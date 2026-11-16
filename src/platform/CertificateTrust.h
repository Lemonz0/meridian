#pragma once
// platform/: server certificate verification with the Windows trust engine.
//
// Why: WebKit's Windows networking (libcurl + LibreSSL) uses
// CURLSSLOPT_NATIVE_CA, which only sees root certificates already present in
// the local Windows store. Windows normally downloads missing roots on demand
// (Automatic Root Certificates Update), but only when CryptoAPI/schannel builds
// a chain. So on a fresh machine, valid sites can fail until something else on
// the system triggers that download. When curl rejects a chain, WebKit asks the
// app (ServerTrustEvaluationRequested); we then verify it with
// CertGetCertificateChain + the SSL policy, which does fetch missing roots.
#include <string>
#include <vector>

namespace meridian::platform {

struct TrustResult {
    bool trusted { false };
    unsigned long policyError { 0 };   // CERT_E_* / TRUST_E_* from the SSL policy
    std::wstring description;
};

// pemChain: leaf first, then intermediates (PEM, as WebKit's curl port supplies them).
TrustResult verifyServerChain(const std::vector<std::string>& pemChain, const std::wstring& host);

} // namespace meridian::platform
