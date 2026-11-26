from pathlib import Path
import hashlib

Import("env")


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))

# Opt-in hosted evidence only; preserve the default build/packing script list.
import os
if os.environ.get('CROSSCUB_PRIVATE_EVIDENCE'):
    import runpy
    runpy.run_path(str(PROJECT_DIR / 'scripts/ci_producer_capture.py'))['install'](env)
MARKER = "/* CrossPoint wolfSSL compatibility overrides */"
OVERRIDES = f"""

{MARKER}
#undef NO_DH
#ifndef HAVE_FFDHE_2048
#define HAVE_FFDHE_2048
#endif
/* MEMFIX-PORT: 8192 handles up to RSA-4096 keys (the public-CA maximum,
   ISRG Root X1 included) with half the per-bignum heap of 16384: with
   WOLFSSL_SMALL_STACK each fast-math temp is FP_MAX_BITS/8 * 2 bytes on the
   heap, and TLS cert verification allocates dozens at once. */
#undef FP_MAX_BITS
#define FP_MAX_BITS 8192
/* Arduino-wolfSSL always defines DEBUG_WOLFSSL, but SecureClient turns the
   trace on only in FREEINK_WOLFSSL_DEBUG builds. Drop it otherwise so the trace
   code and strings are not linked. Set the flag for all sources (build_flags),
   not just SecureNet, to keep the trace. */
#ifndef FREEINK_WOLFSSL_DEBUG
#undef DEBUG_WOLFSSL
#endif
"""


def patch_user_settings(path: Path) -> None:
    original_text = path.read_text()
    text = original_text
    if MARKER in text:
        text = text.split(MARKER, 1)[0].rstrip()
    patched = text + OVERRIDES + "\n"
    if patched == original_text:
        return
    path.write_text(patched)
    print(f"Patched wolfSSL settings: {path.relative_to(PROJECT_DIR)}")


for settings in PROJECT_DIR.glob(".pio/libdeps/*/Arduino-wolfSSL/src/user_settings.h"):
    patch_user_settings(settings)


# Only this exact SDK revision or its exact installed output is accepted.
SDK_SECURENET_PREIMAGES = {'libs/network/SecureNet/src/SecureClient.cpp': 'd2c264bc2d235287f67cfcd550a71e4d7b6bee170c118cb6a646158cd8c3439f', 'libs/network/SecureNet/include/SecureClient.h': '55389390b984cdb46e96b0f49e9b31c36f66fae10f46072637ac11a7e14fa498', 'libs/network/SecureNet/include/SecureHttpClient.h': '02d1982c4567170ce50d40c8c11798665d886ec1c2033195fcf9ed9a407a07b5'}


def _secure_net_transform(path, text):
    def edit(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise RuntimeError("SecureNet overlay anchor mismatch: " + path)
        text = text.replace(old, new)
    if path.endswith("SecureClient.cpp"):
        edit('#include <wolfssl/ssl.h>', '#include <wolfssl/ssl.h>\n#if defined(NO_ASN_TIME) || defined(WOLFSSL_NO_VERIFY_TIME)\n#error "Crosscub verified TLS requires certificate date validation"\n#endif')
        edit('  _rootCA = rootCA;\n  _insecure = false;', '  stop();\n  _rootCA = rootCA;\n  _insecure = false;')
        edit('void SecureClient::setInsecure() { _insecure = true; }', 'void SecureClient::setInsecure() { stop(); _insecure = true; }')
        edit('  if (!_transport.connect(host, port)) {', '  if (!(_usePinnedIP ? _transport.connect(_pinnedIP, port) : _transport.connect(host, port))) {')
        edit('  if (abortRequested() || !hostResolves(host)) return 0;', '  if (abortRequested() || (!_usePinnedIP && !hostResolves(host))) return 0;')
        edit('SecureClient::~SecureClient() { stop(); }', """SecureClient::~SecureClient() { stop(); }

int SecureClient::connectPinned(const char* host, IPAddress vettedAddress, uint16_t port) {
  // Caller vets the address. Keep the original host for SNI and peer identity.
  _pinnedIP = vettedAddress;
  _usePinnedIP = true;
  const int result = connect(host, port);
  _usePinnedIP = false;
  return result;
}""")
        edit("""  if (_insecure) {
    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, nullptr);
  } else if (_rootCA) {
    wolfSSL_CTX_load_verify_buffer(ctx, reinterpret_cast<const unsigned char*>(_rootCA),
                                   strlen(_rootCA), WOLFSSL_FILETYPE_PEM);
  }""", """  // Both auto negotiation and the explicit TLS-1.2 retry use this floor.
  if (wolfSSL_CTX_SetMinVersion(ctx, WOLFSSL_TLSV1_2) != WOLFSSL_SUCCESS) {
    stop();
    return 0;
  }
  if (_insecure) {
    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, nullptr);
  } else {
    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER, nullptr);
    if (!_rootCA || !*_rootCA ||
        wolfSSL_CTX_load_verify_buffer(ctx, reinterpret_cast<const unsigned char*>(_rootCA),
                                     strlen(_rootCA), WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {
      stop();
      return 0;
    }
  }""")
        edit('  if (!_insecure) wolfSSL_check_domain_name(ssl, host);', """  if (!_insecure && wolfSSL_check_domain_name(ssl, host) != WOLFSSL_SUCCESS) {
    stop();
    return 0;
  }""")
    elif path.endswith("SecureClient.h"):
        edit('  int connect(IPAddress ip, uint16_t port) override;', '  int connectPinned(const char* host, IPAddress vettedAddress, uint16_t port);\n  int connect(IPAddress ip, uint16_t port) override;')
        edit('  WiFiClient _transport;', '  IPAddress _pinnedIP;\n  bool _usePinnedIP = false;\n  WiFiClient _transport;')
    else:
        edit('#include <string>', '#include <numeric>\n#include <string>')
        edit('    for (const std::string& h : _headers) size += h.size();',
             '    size = std::accumulate(_headers.begin(), _headers.end(), size,\n'
             '                           [](size_t total, const std::string& header) { return total + header.size(); });')
        edit('  void setInsecure() { _insecure = true; }', '  void setInsecure() { closeConnection(); _secure.setInsecure(); _insecure = true; }')
        edit('    _rootCA = rootCA;\n    _insecure = false;', '    closeConnection();\n    _secure.setCACert(rootCA);\n    _rootCA = rootCA;\n    _insecure = false;')
        edit('      } else if (_rootCA) {\n        _secure.setCACert(_rootCA);', '      } else {\n        _secure.setCACert(_rootCA);')
    return text.encode()


def sdk_secure_net_outputs(sdk_root):
    """Read-only preflight: return exact final bytes, reject unknown inputs."""
    outputs = {}
    for path, expected in SDK_SECURENET_PREIMAGES.items():
        current = (sdk_root / path).read_bytes()
        digest = hashlib.sha256(current).hexdigest()
        if digest == expected:
            outputs[path] = _secure_net_transform(path, current.decode())
            continue
        # Validate already-installed output against the immutable expected
        # preimage encoded below, never by permissive marker recognition.
        original = current.decode()
        restored = _secure_net_inverse(path, original)
        if hashlib.sha256(restored.encode()).hexdigest() != expected:
            raise RuntimeError("Unknown SDK SecureNet bytes: " + path)
        candidate = _secure_net_transform(path, restored)
        legacy = candidate
        if path.endswith('SecureHttpClient.h'):
            legacy = candidate.replace(b'#include <numeric>\n#include <string>', b'#include <string>').replace(
                b'    size = std::accumulate(_headers.begin(), _headers.end(), size,\n'
                b'                           [](size_t total, const std::string& header) { return total + header.size(); });',
                b'    for (const std::string& h : _headers) size += h.size();')
        if current not in (candidate, legacy):
            raise RuntimeError("Invalid installed SecureNet overlay: " + path)
        outputs[path] = candidate
    return outputs

SDK_SECURENET_INVERSE = {'libs/network/SecureNet/src/SecureClient.cpp': [('// integration point for the TLS 1.3 transport.\n#if defined(FREEINK_NET_WOLFSSL)\n#include <wolfssl/ssl.h>\n#if defined(NO_ASN_TIME) || defined(WOLFSSL_NO_VERIFY_TIME)\n#error "Crosscub verified TLS requires certificate date validation"\n#endif\n#if defined(FREEINK_WOLFSSL_RETAIN_INPUT_BUFFER)\n#include <wolfssl/internal.h>\n#include <atomic>\n', '// integration point for the TLS 1.3 transport.\n#if defined(FREEINK_NET_WOLFSSL)\n#include <wolfssl/ssl.h>\n#if defined(FREEINK_WOLFSSL_RETAIN_INPUT_BUFFER)\n#include <wolfssl/internal.h>\n#include <atomic>\n'), ('\nSecureClient::~SecureClient() { stop(); }\n\nint SecureClient::connectPinned(const char* host, IPAddress vettedAddress, uint16_t port) {\n  // Caller vets the address. Keep the original host for SNI and peer identity.\n  _pinnedIP = vettedAddress;\n  _usePinnedIP = true;\n  const int result = connect(host, port);\n  _usePinnedIP = false;\n  return result;\n}\n\nvoid SecureClient::setCACert(const char* rootCA) {\n  stop();\n  _rootCA = rootCA;\n  _insecure = false;\n}\nvoid SecureClient::setInsecure() { stop(); _insecure = true; }\n\n#if defined(FREEINK_NET_WOLFSSL)\n\n', '\nSecureClient::~SecureClient() { stop(); }\n\nvoid SecureClient::setCACert(const char* rootCA) {\n  _rootCA = rootCA;\n  _insecure = false;\n}\nvoid SecureClient::setInsecure() { _insecure = true; }\n\n#if defined(FREEINK_NET_WOLFSSL)\n\n'), ('  if (abortRequested()) return 0;\n  _transport.setConnectionTimeout(timeoutMs);\n  _tcpConnected = false;\n  if (!(_usePinnedIP ? _transport.connect(_pinnedIP, port) : _transport.connect(host, port))) {\n    if (Serial) Serial.printf("[SecureClient] TCP connect failed (%s): %s:%u\\n", label, host, port);\n    return 0;\n  }\n', '  if (abortRequested()) return 0;\n  _transport.setConnectionTimeout(timeoutMs);\n  _tcpConnected = false;\n  if (!_transport.connect(host, port)) {\n    if (Serial) Serial.printf("[SecureClient] TCP connect failed (%s): %s:%u\\n", label, host, port);\n    return 0;\n  }\n'), ('  }\n  _ctx = ctx;\n\n  // Both auto negotiation and the explicit TLS-1.2 retry use this floor.\n  if (wolfSSL_CTX_SetMinVersion(ctx, WOLFSSL_TLSV1_2) != WOLFSSL_SUCCESS) {\n    stop();\n    return 0;\n  }\n  if (_insecure) {\n    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, nullptr);\n  } else {\n    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER, nullptr);\n    if (!_rootCA || !*_rootCA ||\n        wolfSSL_CTX_load_verify_buffer(ctx, reinterpret_cast<const unsigned char*>(_rootCA),\n                                     strlen(_rootCA), WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {\n      stop();\n      return 0;\n    }\n  }\n  wolfSSL_SetIORecv(ctx, wcRecv);\n  wolfSSL_SetIOSend(ctx, wcSend);\n', '  }\n  _ctx = ctx;\n\n  if (_insecure) {\n    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, nullptr);\n  } else if (_rootCA) {\n    wolfSSL_CTX_load_verify_buffer(ctx, reinterpret_cast<const unsigned char*>(_rootCA),\n                                   strlen(_rootCA), WOLFSSL_FILETYPE_PEM);\n  }\n  wolfSSL_SetIORecv(ctx, wcRecv);\n  wolfSSL_SetIOSend(ctx, wcSend);\n'), ("  wolfSSL_SetIOWriteCtx(ssl, &_transport);\n  wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME, host, strlen(host));\n  // Chain verification alone accepts any certificate under the pinned root.\n  if (!_insecure && wolfSSL_check_domain_name(ssl, host) != WOLFSSL_SUCCESS) {\n    stop();\n    return 0;\n  }\n#if defined(WOLFSSL_TLS13) && defined(HAVE_CURVE25519)\n  // MEMFIX-PORT: pin the TLS 1.3 key_share to X25519. wolfSSL's default is a\n  // P-256 share, generated with fast-math bignums that WOLFSSL_SMALL_STACK\n", "  wolfSSL_SetIOWriteCtx(ssl, &_transport);\n  wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME, host, strlen(host));\n  // Chain verification alone accepts any certificate under the pinned root.\n  if (!_insecure) wolfSSL_check_domain_name(ssl, host);\n#if defined(WOLFSSL_TLS13) && defined(HAVE_CURVE25519)\n  // MEMFIX-PORT: pin the TLS 1.3 key_share to X25519. wolfSSL's default is a\n  // P-256 share, generated with fast-math bignums that WOLFSSL_SMALL_STACK\n"), ("int SecureClient::connect(const char* host, uint16_t port) {\n  _aborted = false;\n  stop();\n  if (abortRequested() || (!_usePinnedIP && !hostResolves(host))) return 0;\n  // Negotiate the highest mutually supported version rather than pinning TLS 1.3:\n  // self-hosted / Let's Encrypt nginx often tops out at TLS 1.2, and a 1.3-only\n  // client fails those handshakes outright. v23 still selects 1.3 when the peer\n", "int SecureClient::connect(const char* host, uint16_t port) {\n  _aborted = false;\n  stop();\n  if (abortRequested() || !hostResolves(host)) return 0;\n  // Negotiate the highest mutually supported version rather than pinning TLS 1.3:\n  // self-hosted / Let's Encrypt nginx often tops out at TLS 1.2, and a 1.3-only\n  // client fails those handshakes outright. v23 still selects 1.3 when the peer\n")], 'libs/network/SecureNet/include/SecureClient.h': [('  bool aborted() const { return _aborted; }\n\n  // Connect and perform a TLS 1.3 handshake to host:port (uses the SNI host).\n  int connectPinned(const char* host, IPAddress vettedAddress, uint16_t port);\n  int connect(IPAddress ip, uint16_t port) override;\n  int connect(const char* host, uint16_t port) override;\n\n', '  bool aborted() const { return _aborted; }\n\n  // Connect and perform a TLS 1.3 handshake to host:port (uses the SNI host).\n  int connect(IPAddress ip, uint16_t port) override;\n  int connect(const char* host, uint16_t port) override;\n\n'), ('  // a failure after connect() returns 0.\n  bool abortRequested();\n\n  IPAddress _pinnedIP;\n  bool _usePinnedIP = false;\n  WiFiClient _transport;\n  AbortCallback _shouldAbort;\n  bool _aborted = false;\n', '  // a failure after connect() returns 0.\n  bool abortRequested();\n\n  WiFiClient _transport;\n  AbortCallback _shouldAbort;\n  bool _aborted = false;\n')], 'libs/network/SecureNet/include/SecureHttpClient.h': [('\n  // Skip peer verification (SecureClient does likewise). Required today because\n  // the wolfSSL transport has no CA bundle wired up; see setCACert().\n  void setInsecure() { closeConnection(); _secure.setInsecure(); _insecure = true; }\n  // Bulk download: lend wolfSSL a full-record buffer once the request is sent\n  // (see SecureClient::reserveFullRecord()). Applies to https connections.\n  void setReserveFullRecord(bool on) { _reserveFullRecord = on; }\n  // Verify against a single PEM root. Clears the insecure flag.\n  void setCACert(const char* rootCA) {\n    closeConnection();\n    _secure.setCACert(rootCA);\n    _rootCA = rootCA;\n    _insecure = false;\n  }\n', '\n  // Skip peer verification (SecureClient does likewise). Required today because\n  // the wolfSSL transport has no CA bundle wired up; see setCACert().\n  void setInsecure() { _insecure = true; }\n  // Bulk download: lend wolfSSL a full-record buffer once the request is sent\n  // (see SecureClient::reserveFullRecord()). Applies to https connections.\n  void setReserveFullRecord(bool on) { _reserveFullRecord = on; }\n  // Verify against a single PEM root. Clears the insecure flag.\n  void setCACert(const char* rootCA) {\n    _rootCA = rootCA;\n    _insecure = false;\n  }\n'), ('    if (_scheme == "https") {\n      if (_insecure) {\n        _secure.setInsecure();\n      } else {\n        _secure.setCACert(_rootCA);\n      }\n      _secure.setTimeout(_timeoutMs);\n', '    if (_scheme == "https") {\n      if (_insecure) {\n        _secure.setInsecure();\n      } else if (_rootCA) {\n        _secure.setCACert(_rootCA);\n      }\n      _secure.setTimeout(_timeoutMs);\n')]}


def _secure_net_inverse(path, text):
    # Accept the previous exact TLS overlay as well as the accumulated-size
    # successor; immutable upstream hash verification still follows inversion.
    if path.endswith('SecureHttpClient.h'):
        text = text.replace('#include <numeric>\n#include <string>', '#include <string>')
        text = text.replace(
            '    size = std::accumulate(_headers.begin(), _headers.end(), size,\n'
            '                           [](size_t total, const std::string& header) { return total + header.size(); });',
            '    for (const std::string& h : _headers) size += h.size();')
    for installed, original in SDK_SECURENET_INVERSE[path]:
        if not installed or text.count(installed) != 1:
            raise RuntimeError("Unknown installed SecureNet bytes: " + path)
        text = text.replace(installed, original)
    return text


_sdk_root = PROJECT_DIR / "freeink-sdk"
if (_sdk_root / next(iter(SDK_SECURENET_PREIMAGES))).is_file():
    # The same exact local fixes run for metadata, check and firmware builds.
    import runpy
    _analyzer = runpy.run_path(str(PROJECT_DIR / 'scripts/cppcheck_exact.py'))
    # Authenticate both families completely before writing either one.
    _outputs = _analyzer['prepare_patches'](_sdk_root)
    _outputs.update(sdk_secure_net_outputs(_sdk_root))
    for _path, _data in _outputs.items():
        _target = _sdk_root / _path
        if _target.read_bytes() != _data:
            _target.write_bytes(_data)
            if _path in SDK_SECURENET_PREIMAGES:
                print("Applied revision-bound Crosscub SecureNet overlay: " + _path)
