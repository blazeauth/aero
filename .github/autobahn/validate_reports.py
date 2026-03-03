import argparse
import asyncio
import json
import os
from dataclasses import dataclass
from pathlib import Path

import websockets

@dataclass(frozen=True)
class CaseResult:
  case_id: str
  behavior: str
  behavior_close: str
  duration: int | None
  remote_close_code: int | None
  reportfile: str | None

def normalize_cases_tree(index_data: object, agent: str) -> dict[str, dict]:
  if isinstance(index_data, dict) and agent in index_data and isinstance(index_data[agent], dict):
    return index_data[agent]
  if isinstance(index_data, dict):
    return index_data
  raise RuntimeError("Unexpected index.json structure")

def load_results(index_json_path: Path, agent: str) -> list[CaseResult]:
  index_data = json.loads(index_json_path.read_text(encoding="utf-8"))
  cases_tree = normalize_cases_tree(index_data, agent)

  results: list[CaseResult] = []
  for case_id, payload in cases_tree.items():
    if not isinstance(payload, dict):
      continue
    results.append(
      CaseResult(
        case_id=str(case_id),
        behavior=str(payload.get("behavior", "")),
        behavior_close=str(payload.get("behaviorClose", "")),
        duration=payload.get("duration"),
        remote_close_code=payload.get("remoteCloseCode"),
        reportfile=payload.get("reportfile"),
      )
    )
  return results

def should_fail(result: CaseResult, allow_nonstrict_prefix: str) -> tuple[bool, str]:
  if result.behavior == "OK":
    return (False, "")
  if result.behavior == "INFORMATIONAL":
    return (False, "")
  if result.behavior == "NON-STRICT":
    if result.case_id.startswith(allow_nonstrict_prefix + ".") or result.case_id == allow_nonstrict_prefix:
      if result.behavior_close != "OK":
        return (True, "NON-STRICT allowed here, but behaviorClose is not OK")
      return (False, "")
    return (True, "NON-STRICT is not allowed")
  return (True, f"behavior is {result.behavior}")

async def ws_text(server_base_uri: str, path: str) -> str:
  url = server_base_uri.rstrip("/") + "/" + path.lstrip("/")
  async with websockets.connect(url, max_size=16 * 1024 * 1024) as ws:
    message = await ws.recv()
    if not isinstance(message, str):
      raise RuntimeError("Expected text message")
    return message

async def fetch_case_info_map(server_base_uri: str, wanted_case_ids: set[str]) -> dict[str, dict]:
  if not wanted_case_ids:
    return {}

  case_count_text = await ws_text(server_base_uri, "getCaseCount")
  case_count = int(case_count_text.strip())

  found: dict[str, dict] = {}
  for i in range(1, case_count + 1):
    info_text = await ws_text(server_base_uri, f"getCaseInfo?case={i}")
    info = json.loads(info_text)
    case_id = str(info.get("id") or info.get("case") or info.get("case_id") or "")
    if case_id in wanted_case_ids:
      found[case_id] = info
      if len(found) == len(wanted_case_ids):
        break

  return found

def format_case_line(result: CaseResult) -> str:
  parts = [f"Case {result.case_id}", f"behavior={result.behavior}"]
  if result.behavior_close:
    parts.append(f"behaviorClose={result.behavior_close}")
  if result.remote_close_code is not None:
    parts.append(f"remoteCloseCode={result.remote_close_code}")
  if result.duration is not None:
    parts.append(f"duration={result.duration}ms")
  if result.reportfile:
    parts.append(f"reportfile={result.reportfile}")
  return " | ".join(parts)

async def main_async() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--agent", required=True)
  parser.add_argument("--server", required=True)
  parser.add_argument("--index", required=True)
  parser.add_argument("--allow-nonstrict-prefix", default="6.4")
  args = parser.parse_args()

  index_json_path = Path(args.index)
  results = load_results(index_json_path, args.agent)

  failures: list[tuple[CaseResult, str]] = []
  for result in results:
    failed, reason = should_fail(result, args.allow_nonstrict_prefix)
    if failed:
      failures.append((result, reason))

  if not failures:
    return 0

  wanted = {r.case_id for r, _ in failures}
  info_map = await fetch_case_info_map(args.server, wanted)

  print("Autobahn failures detected:")
  for result, reason in failures:
    print(format_case_line(result))
    print(f"Reason: {reason}")
    info = info_map.get(result.case_id, {})
    description = str(info.get("description", "")).strip()
    expectation = str(info.get("expectation", "")).strip()
    if description:
      print(f"Description: {description}")
    if expectation:
      print(f"Expectation: {expectation}")
    print("")

  return 1

def main() -> None:
  raise SystemExit(asyncio.run(main_async()))

if __name__ == "__main__":
  main()
