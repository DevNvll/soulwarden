#include "service/tls_identity.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>

#include "core/logging/log.h"

namespace soulwarden::backend::service {
namespace {

bool read_file(const std::filesystem::path& path, std::string& content) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    content = stream.str();
    return !content.empty();
}

bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
    return static_cast<bool>(output);
}

bool generate(TlsIdentity& identity) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_pk_context key;
    mbedtls_x509write_cert certificate;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&random);
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&certificate);

    static constexpr unsigned char kPersonalization[] = "soulwarden-local-identity";
    static constexpr unsigned char kSerial[] = {0x53, 0x4F, 0x55, 0x4C, 0x00, 0x01};
    std::vector<unsigned char> buffer(8192);
    bool ok = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, kPersonalization, sizeof(kPersonalization) - 1) == 0
              && mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) == 0
              && mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &random, 2048, 65537) == 0;
    if (ok) {
        mbedtls_x509write_crt_set_version(&certificate, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&certificate, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&certificate, &key);
        mbedtls_x509write_crt_set_issuer_key(&certificate, &key);
        ok = mbedtls_x509write_crt_set_subject_name(&certificate, "CN=Soulwarden Local,O=Soulwarden") == 0
             && mbedtls_x509write_crt_set_issuer_name(&certificate, "CN=Soulwarden Local,O=Soulwarden") == 0
             && mbedtls_x509write_crt_set_serial_raw(&certificate, const_cast<unsigned char*>(kSerial), sizeof(kSerial)) == 0
             && mbedtls_x509write_crt_set_validity(&certificate, "20250101000000", "20491231235959") == 0
             && mbedtls_x509write_crt_set_basic_constraints(&certificate, 1, -1) == 0;
    }
    if (ok) {
        ok = mbedtls_x509write_crt_pem(&certificate, buffer.data(), buffer.size(), mbedtls_ctr_drbg_random, &random) == 0;
        if (ok) {
            identity.certificatePem = reinterpret_cast<const char*>(buffer.data());
        }
    }
    if (ok) {
        ok = mbedtls_pk_write_key_pem(&key, buffer.data(), buffer.size()) == 0;
        if (ok) {
            identity.privateKeyPem = reinterpret_cast<const char*>(buffer.data());
        }
    }
    mbedtls_x509write_crt_free(&certificate);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&random);
    mbedtls_entropy_free(&entropy);
    return ok;
}

} // namespace

bool load_or_create_identity(const std::string& directory, TlsIdentity& identity) {
    const std::filesystem::path root = directory;
    const std::filesystem::path certificatePath = root / "server-cert.pem";
    const std::filesystem::path keyPath = root / "server-key.pem";
    if (read_file(certificatePath, identity.certificatePem) && read_file(keyPath, identity.privateKeyPem)) {
        return true;
    }
    std::filesystem::create_directories(root);
    if (!generate(identity)) {
        log::write("tls: the local identity could not be made");
        return false;
    }
    if (!write_file(certificatePath, identity.certificatePem) || !write_file(keyPath, identity.privateKeyPem)) {
        log::write("tls: the local identity could not be saved");
        return false;
    }
    std::error_code ignored;
    std::filesystem::permissions(keyPath,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 ignored);
    log::write("tls: made a new local identity in %s", directory.c_str());
    return true;
}

} // namespace soulwarden::backend::service
