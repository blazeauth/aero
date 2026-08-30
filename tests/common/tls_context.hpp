#pragma once

#include <string_view>

#include <asio/buffer.hpp>
#include <asio/ssl.hpp>

namespace aero::tests {

  // Self-signed, CN=aero-test, valid until 2126
  constexpr std::string_view test_certificate = "-----BEGIN CERTIFICATE-----\n"
                                                "MIIDCzCCAfOgAwIBAgIUYoFNZqaE+kHhfao5QiVrKxbAjycwDQYJKoZIhvcNAQEL\n"
                                                "BQAwFDESMBAGA1UEAwwJYWVyby10ZXN0MCAXDTI2MDgzMDIwMDUwMFoYDzIxMjYw\n"
                                                "ODA2MjAwNTAwWjAUMRIwEAYDVQQDDAlhZXJvLXRlc3QwggEiMA0GCSqGSIb3DQEB\n"
                                                "AQUAA4IBDwAwggEKAoIBAQDP7vWrF14tlkHQTachhoBwGKF75awUgrrfREJly+Jr\n"
                                                "yW5PL2GRIk2YUSQTTBzdswT4+riuc0QLyBH9mLhcVSIhNffH0Iqr5VhB9Z9wmaHW\n"
                                                "wQZ6X9dQRrSLoiM0oH6Oi+cC1NWtA9P3LhpItRfxfhWNi9EW1sEpRE08ixNF50EH\n"
                                                "7kaK1BEzsk4dWboUA5nbs1ZEA3s/rir5EzaYCNmfxkUeB1RfhHQ2/IwXrIbg9bnb\n"
                                                "VhN0OdW/L6uicagwhfj8sQ65a4HNZ/nvWzhe97ztQIdSHctvtq00B88RmbUf//3j\n"
                                                "UWC1FohSQnGegTNvJUUH79BGPVeWfjMjOeQ1WdpXCdvBAgMBAAGjUzBRMB0GA1Ud\n"
                                                "DgQWBBQv22/uSVKcJKT1OAPUm9gLF4NUVjAfBgNVHSMEGDAWgBQv22/uSVKcJKT1\n"
                                                "OAPUm9gLF4NUVjAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQBO\n"
                                                "6oebgziWB7KG7TM68NCreWQ6GuVfpXg2HxidoHbxbgVm2e3s/XX9f3CrBXyKl1cU\n"
                                                "VVNiFIrVLFuCzcONQbU6kwYt4BdElN7ElzeeZPfXhdBMK61MoHSIoLokJ2+MN0o1\n"
                                                "F+wyG6fqOhP/pmzNoKkFVU8YZUg4TnCSe0/s+qZdNw2DqFAzLSd4/9QrbfIfqzYz\n"
                                                "HRgOya+fjBqRVWwHyfiDrdE+R2lIvxHbCOClPbYX0rMO8qKy3sC+aKmMmaaVZZvD\n"
                                                "hKYfY1G97de7RgBY3tHXF8oI4dIYskoT+Y1XsipzSoiSsKiVjLz4TC7lg1V1oSPU\n"
                                                "lA2oHezQVVpsYnep3Ggv\n"
                                                "-----END CERTIFICATE-----\n";

  constexpr std::string_view test_private_key = "-----BEGIN PRIVATE KEY-----\n"
                                                "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQDP7vWrF14tlkHQ\n"
                                                "TachhoBwGKF75awUgrrfREJly+JryW5PL2GRIk2YUSQTTBzdswT4+riuc0QLyBH9\n"
                                                "mLhcVSIhNffH0Iqr5VhB9Z9wmaHWwQZ6X9dQRrSLoiM0oH6Oi+cC1NWtA9P3LhpI\n"
                                                "tRfxfhWNi9EW1sEpRE08ixNF50EH7kaK1BEzsk4dWboUA5nbs1ZEA3s/rir5EzaY\n"
                                                "CNmfxkUeB1RfhHQ2/IwXrIbg9bnbVhN0OdW/L6uicagwhfj8sQ65a4HNZ/nvWzhe\n"
                                                "97ztQIdSHctvtq00B88RmbUf//3jUWC1FohSQnGegTNvJUUH79BGPVeWfjMjOeQ1\n"
                                                "WdpXCdvBAgMBAAECggEAGUqsGBz2CB58L92aJCJLkhb04XCcvzvthgWz+9TSXCD6\n"
                                                "qWgOeoxNGudXt38tDaxeQPiiKRn9H1+9DHcciaKTTa3WTzgm/eSeGRvKwnP1cv00\n"
                                                "kAMDWhDXmhplJNwWuLj8puQDf5F1IV46tThNysJ21ao5iwkhIqdbq68Q75JC3zdc\n"
                                                "k6QMwf/Bqn2c8ffV0RZa7KlM3FU0L3/9kIPAlt9FsNG7nN/PeCP8KkeVaySsWRFt\n"
                                                "K/yG9oJuelpOCD7HKte6rqhb/WkTnMBikgq9GtF483VFyo6DnHgXFXB9jjpO1bPJ\n"
                                                "RElf9Gly576SpluP4526H0KzIGQzL/ySXc5D+fvQJwKBgQD5D5zl/7bufW7TdPBl\n"
                                                "pQ3Cgx1Wm0xwvLSYK8nLah/dYp4OS4cTpXJIO+t6IECLXs2nTPun8v3b/8l6LnVn\n"
                                                "V4tmluflzgLndhLyUfcM5diEDNJxsyeTnRIsjK6bfMJbpCZhpjVtvXSchv4ovh2v\n"
                                                "M6xE9+GU29229cpCNQsJxlriswKBgQDVugLbjsjy2GG+BrL/AJvYmzSdLeTA8PF5\n"
                                                "2CgXIiUcTvachtE5lUyIB1+f2HQR5Zo44qwXksLnhPVGKA/p09AJ+ZlYmM2esNcf\n"
                                                "zgxrtCWwGLp5dHwEw5eR7mEzd3kjx6Z6Kpbpa8PRjZTJ0pkzO3GWrMvsgcYe4Q9H\n"
                                                "O9f/lrYxuwKBgFD/9OUQTyws+xgmVfCYx2rVPXtnMmsP1CQRSaWwNADKC+FWSu3m\n"
                                                "xs4bPrAPQS6SfIvGi6nJaypbe+kSpvgfDqUkuvKQF32zduH8Kj61mb8IdICp5Vsq\n"
                                                "oDiA4GCNKKCpOBpV9dZk4UHu3UXe3sSWJ5aej2zcPLU+JrN1kMtzCSflAoGAJnB1\n"
                                                "ER4GID8wnSfBS8HFRdjsRpS5fsYW+C4bT8XRXN0K164btTqX8CM7XJlmjs13xmFm\n"
                                                "SDsaGN+96WdNLWXuFc0xelDJMpBlsI+zhi95U8muyCdeItE20oVIMCR9wiSnWXON\n"
                                                "ft/l8SuApifda+x2Cn57ksboZideQxaNS6fEjv8CgYAnIl8pDsGNGRdyLfeF5Smu\n"
                                                "3moWxTYyurbUi6at5NUi/pBAWYs1DM9s3Wby3fKnakkqINFaF30V1/qxFkcDwmFN\n"
                                                "/HoaqftFcfaofW6yceVIuaKXowy/rAYJNoc/W9x51GuqZfsdwQNuVyykgVzPkC8z\n"
                                                "7V9BJZYR1/PVj2VJ4A92AQ==\n"
                                                "-----END PRIVATE KEY-----\n";

  inline asio::ssl::context make_tls_server_context() {
    asio::ssl::context ctx{asio::ssl::context::tls_server};
    ctx.use_certificate_chain(asio::buffer(test_certificate));
    ctx.use_private_key(asio::buffer(test_private_key), asio::ssl::context::pem);
    return ctx;
  }

  inline asio::ssl::context make_tls_client_context() {
    asio::ssl::context ctx{asio::ssl::context::tls_client};
    ctx.add_certificate_authority(asio::buffer(test_certificate));
    ctx.set_verify_mode(asio::ssl::verify_peer);
    return ctx;
  }

} // namespace aero::tests
