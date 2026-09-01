#include "Crypto.h"

#ifdef MSG_HAVE_SODIUM
  #include <sodium.h>
#endif

#include <cstring>
#include <stdexcept>

namespace msg::crypto {

bool initLibrary() {
#ifdef MSG_HAVE_SODIUM
    return sodium_init() >= 0;
#else
    // Scaffold build without libsodium linked - real crypto is disabled.
    // main.cpp should refuse to start a real session in this mode; it's
    // only here so the rest of the client links and the UI can be built.
    return false;
#endif
}

KeyPair generateIdentityKeyPair() {
    KeyPair kp;
#ifdef MSG_HAVE_SODIUM
    kp.publicKey.resize(crypto_box_PUBLICKEYBYTES);
    kp.secretKey.resize(crypto_box_SECRETKEYBYTES);
    crypto_box_keypair(kp.publicKey.data(), kp.secretKey.data());
#endif
    return kp;
}

std::optional<Bytes> deriveInitialSharedSecret(const KeyPair& ourIdentity,
                                                const Bytes& theirIdentityPublic,
                                                const Bytes& theirSignedPrekeyPublic,
                                                const Bytes& theirOneTimePrekeyPublic) {
#ifdef MSG_HAVE_SODIUM
    // Simplified X3DH: DH1 = our_identity x their_signed_prekey
    //                   DH2 = our_identity x their_identity
    //                   DH3 = our_identity x their_one_time_prekey (if present)
    // shared_secret = HKDF(DH1 || DH2 || DH3)
    if (ourIdentity.secretKey.size() != crypto_box_SECRETKEYBYTES) return std::nullopt;

    auto dh = [&](const Bytes& theirPub) -> std::optional<Bytes> {
        if (theirPub.size() != crypto_box_PUBLICKEYBYTES) return std::nullopt;
        Bytes shared(crypto_scalarmult_BYTES);
        if (crypto_scalarmult(shared.data(), ourIdentity.secretKey.data(), theirPub.data()) != 0)
            return std::nullopt;
        return shared;
    };

    auto dh1 = dh(theirSignedPrekeyPublic);
    auto dh2 = dh(theirIdentityPublic);
    if (!dh1 || !dh2) return std::nullopt;

    Bytes combined;
    combined.insert(combined.end(), dh1->begin(), dh1->end());
    combined.insert(combined.end(), dh2->begin(), dh2->end());

    if (!theirOneTimePrekeyPublic.empty()) {
        if (auto dh3 = dh(theirOneTimePrekeyPublic))
            combined.insert(combined.end(), dh3->begin(), dh3->end());
    }

    Bytes out(crypto_kdf_KEYBYTES);
    crypto_generichash(out.data(), out.size(), combined.data(), combined.size(), nullptr, 0);
    return out;
#else
    (void)ourIdentity; (void)theirIdentityPublic; (void)theirSignedPrekeyPublic; (void)theirOneTimePrekeyPublic;
    return std::nullopt;
#endif
}

RatchetStep ratchetChain(const ChainKey& current) {
    RatchetStep step;
#ifdef MSG_HAVE_SODIUM
    // KDF_CK(ck) -> (ck', mk) via HMAC-like BLAKE2b with distinct context bytes.
    Bytes ck_input = current.key;
    ck_input.push_back(0x01);
    step.nextChainKey.key.resize(crypto_generichash_BYTES);
    crypto_generichash(step.nextChainKey.key.data(), step.nextChainKey.key.size(),
                        ck_input.data(), ck_input.size(), nullptr, 0);
    step.nextChainKey.index = current.index + 1;

    Bytes mk_input = current.key;
    mk_input.push_back(0x02);
    step.messageKey.resize(crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    crypto_generichash(step.messageKey.data(), step.messageKey.size(),
                        mk_input.data(), mk_input.size(), nullptr, 0);
#else
    step.nextChainKey = current;
    step.nextChainKey.index = current.index + 1;
#endif
    return step;
}

Bytes encryptMessage(const Bytes& messageKey, const Bytes& plaintext, const Bytes& aad) {
#ifdef MSG_HAVE_SODIUM
    Bytes nonce(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    randombytes_buf(nonce.data(), nonce.size());

    Bytes ciphertext(plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long clen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext.data(), &clen,
        plaintext.data(), plaintext.size(),
        aad.data(), aad.size(),
        nullptr, nonce.data(), messageKey.data());
    ciphertext.resize(clen);

    // Prepend nonce so decrypt() can pull it back out.
    Bytes out;
    out.insert(out.end(), nonce.begin(), nonce.end());
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    return out;
#else
    throw std::runtime_error("Crypto not available: build without MSG_HAVE_SODIUM cannot encrypt real messages.");
#endif
}

std::optional<Bytes> decryptMessage(const Bytes& messageKey, const Bytes& ciphertext, const Bytes& aad) {
#ifdef MSG_HAVE_SODIUM
    constexpr size_t NPUB = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    if (ciphertext.size() < NPUB) return std::nullopt;

    Bytes nonce(ciphertext.begin(), ciphertext.begin() + NPUB);
    Bytes body(ciphertext.begin() + NPUB, ciphertext.end());

    Bytes plaintext(body.size());
    unsigned long long plen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            plaintext.data(), &plen, nullptr,
            body.data(), body.size(),
            aad.data(), aad.size(),
            nonce.data(), messageKey.data()) != 0) {
        return std::nullopt; // auth failed / tampered
    }
    plaintext.resize(plen);
    return plaintext;
#else
    (void)messageKey; (void)ciphertext; (void)aad;
    return std::nullopt;
#endif
}

std::optional<Bytes> deriveVaultKey(const std::string& password, const Bytes& salt) {
#ifdef MSG_HAVE_SODIUM
    if (salt.size() != crypto_pwhash_SALTBYTES) return std::nullopt; // VAULT_SALT_BYTES must match this

    Bytes key(VAULT_KEY_BYTES);
    // OPSLIMIT_INTERACTIVE/MEMLIMIT_INTERACTIVE, not _MODERATE or
    // _SENSITIVE: this runs on every login on what may be a modest
    // machine, and the vault key only protects locally-cached chat
    // history (not the account itself - that's the server-side password
    // hash, which does use a stronger profile - see PasswordHasher on the
    // server). Interactive limits are still Argon2id, still far stronger
    // than an unsalted hash, just tuned for "runs in well under a second
    // on login" rather than "resists a well-funded attacker for hours".
    if (crypto_pwhash(key.data(), key.size(),
                       password.c_str(), password.size(),
                       salt.data(),
                       crypto_pwhash_OPSLIMIT_INTERACTIVE,
                       crypto_pwhash_MEMLIMIT_INTERACTIVE,
                       crypto_pwhash_ALG_ARGON2ID13) != 0) {
        return std::nullopt; // most likely: ran out of memory for the KDF
    }
    return key;
#else
    (void)password; (void)salt;
    return std::nullopt;
#endif
}

Bytes generateVaultSalt() {
    Bytes salt(VAULT_SALT_BYTES);
#ifdef MSG_HAVE_SODIUM
    randombytes_buf(salt.data(), salt.size());
#endif
    return salt;
}

} // namespace msg::crypto
