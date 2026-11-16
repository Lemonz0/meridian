#include "CertificateTrust.h"

#include <windows.h>
#include <wincrypt.h>

namespace meridian::platform {

static std::vector<BYTE> pemToDer(const std::string& pem)
{
    DWORD size = 0;
    if (!CryptStringToBinaryA(pem.data(), static_cast<DWORD>(pem.size()), CRYPT_STRING_BASE64HEADER, nullptr, &size, nullptr, nullptr))
        return {};
    std::vector<BYTE> der(size);
    if (!CryptStringToBinaryA(pem.data(), static_cast<DWORD>(pem.size()), CRYPT_STRING_BASE64HEADER, der.data(), &size, nullptr, nullptr))
        return {};
    der.resize(size);
    return der;
}

static std::wstring describe(DWORD err)
{
    switch (err) {
    case 0: return L"OK";
    case CERT_E_EXPIRED: return L"The certificate has expired or is not yet valid.";
    case CERT_E_UNTRUSTEDROOT: return L"The certificate is not issued by a trusted authority.";
    case CERT_E_CN_NO_MATCH: return L"The certificate is for a different website.";
    case CERT_E_REVOKED: return L"The certificate has been revoked.";
    case CERT_E_WRONG_USAGE: return L"The certificate is not valid for websites.";
    case CERT_E_CHAINING: return L"The certificate chain is incomplete.";
    case TRUST_E_CERT_SIGNATURE: return L"The certificate signature is invalid.";
    default: return L"The certificate could not be verified.";
    }
}

TrustResult verifyServerChain(const std::vector<std::string>& pemChain, const std::wstring& host)
{
    TrustResult result;
    if (pemChain.empty()) {
        result.description = L"The server sent no certificate.";
        return result;
    }

    // Leaf + intermediates go into an in-memory store used as extra chain material.
    HCERTSTORE extra = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    PCCERT_CONTEXT leaf = nullptr;
    for (size_t i = 0; i < pemChain.size(); ++i) {
        auto der = pemToDer(pemChain[i]);
        if (der.empty())
            continue;
        PCCERT_CONTEXT added = nullptr;
        CertAddEncodedCertificateToStore(extra, X509_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()),
            CERT_STORE_ADD_ALWAYS, i == 0 ? &leaf : nullptr);
        (void)added;
    }
    if (!leaf) {
        CertCloseStore(extra, 0);
        result.description = L"The server certificate could not be read.";
        return result;
    }

    LPSTR usages[] = { const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH) };
    CERT_CHAIN_PARA chainPara { sizeof(chainPara) };
    chainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
    chainPara.RequestedUsage.Usage.cUsageIdentifier = 1;
    chainPara.RequestedUsage.Usage.rgpszUsageIdentifier = usages;

    // Network retrieval is allowed (this is what pulls missing roots and AIA
    // intermediates); revocation is checked for the end certificate only.
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    BOOL built = CertGetCertificateChain(nullptr, leaf, nullptr, extra, &chainPara,
        CERT_CHAIN_REVOCATION_CHECK_END_CERT | CERT_CHAIN_REVOCATION_ACCUMULATIVE_TIMEOUT, nullptr, &chain);

    if (built && chain) {
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl {};
        ssl.cbSize = sizeof(ssl);
        ssl.dwAuthType = AUTHTYPE_SERVER;
        ssl.pwszServerName = const_cast<wchar_t*>(host.c_str());
        // Offline/unknown revocation status is not treated as a hard failure
        // (same as mainstream browsers); a positive "revoked" still fails.
        ssl.fdwChecks = 0;
        CERT_CHAIN_POLICY_PARA policyPara { sizeof(policyPara) };
        policyPara.dwFlags = CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS;
        policyPara.pvExtraPolicyPara = &ssl;
        CERT_CHAIN_POLICY_STATUS status { sizeof(status) };
        if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policyPara, &status)) {
            result.policyError = status.dwError;
            result.trusted = status.dwError == 0;
        } else
            result.policyError = GetLastError();
        CertFreeCertificateChain(chain);
    } else
        result.policyError = GetLastError();

    result.description = describe(result.policyError);
    CertFreeCertificateContext(leaf);
    CertCloseStore(extra, 0);
    return result;
}

} // namespace meridian::platform
