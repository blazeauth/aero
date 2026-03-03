#ifndef AERO_WEBSOCKET_HPP
#define AERO_WEBSOCKET_HPP

#include "aero/websocket/client.hpp"
#include "aero/websocket/client_handshaker.hpp"
#include "aero/websocket/client_options.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"
#include "aero/websocket/state.hpp"
#include "aero/websocket/uri.hpp"

#ifdef AERO_USE_TLS
#include "aero/websocket/tls/client.hpp"
#endif

#endif
