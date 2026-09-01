#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace msg::crypto {

// IMPORTANT: This deliberately does NOT invent a new cipher. All primitives
// below are libsodium's well-audited implementations of X25519 (key
// exchange), XChaCha20-Poly1305 (AEAD), and HKDF/BLAKE2b (key derivation).
// "Unique" here means our own session/ratchet *protocol* wiring on top of
// standard primitives - not a home-grown algorithm, which would be a
// security liability rather than a feature.

using Bytes = std::vector<uint8_t>;

struct KeyPair {
    Bytes publicKey;
    Bytes secretKey;
};

// One ratchet step's derived symmetric key material.
struct ChainKey {
    Bytes key;
    uint64_t index = 0;
};

// Generates a long-term identity keypair (X25519) for this device.
// Called once at first run; secretKey is persisted encrypted-at-rest
// under the user's local login password (see LocalVault, not yet in this
// scaffold pass).
KeyPair generateIdentityKeyPair();

// X3DH-style initial handshake: derives an initial shared secret from our
// identity key + their published prekey bundle. Returns std::nullopt on
// failure (bad key sizes, libsodium init failure, etc).
std::optional<Bytes> deriveInitialSharedSecret(const KeyPair& ourIdentity,
                                                const Bytes& theirIdentityPublic,
                                                const Bytes& theirSignedPrekeyPublic,
                                                const Bytes& theirOneTimePrekeyPublic);

// Double-Ratchet-style symmetric-key ratchet step: given the current chain
// key, derives the next chain key + a message key, so every message uses a
// fresh key (forward secrecy) without a fresh DH exchange each time.
struct RatchetStep {
    ChainKey nextChainKey;
    Bytes messageKey;
};
RatchetStep ratchetChain(const ChainKey& current);

// AEAD encrypt/decrypt of a single message using a per-message key from the
// ratchet above. `aad` should include sender/recipient IDs + timestamp to
// bind ciphertext to context.
Bytes encryptMessage(const Bytes& messageKey, const Bytes& plaintext, const Bytes& aad);
std::optional<Bytes> decryptMessage(const Bytes& messageKey, const Bytes& ciphertext, const Bytes& aad);

// --- Local vault key derivation -----------------------------------------
// Turns the user's login password into a symmetric key for encrypting
// LocalStore's on-disk file (see LocalStore.h). Argon2id via libsodium's
// crypto_pwhash - the same primitive the server uses for password hashing
// (PasswordHasher.cpp on the server side), not a separate home-grown KDF.
// `salt` must be VAULT_SALT_BYTES long and does NOT need to be secret -
// it just needs to be unique per account and stored alongside the vault
// (see LocalStore's salt file). Returns nullopt if libsodium isn't
// available or the derivation fails (e.g. out of memory for the KDF's
// memory-hardness parameters).
constexpr size_t VAULT_SALT_BYTES = 16;
constexpr size_t VAULT_KEY_BYTES = 32;
std::optional<Bytes> deriveVaultKey(const std::string& password, const Bytes& salt);
Bytes generateVaultSalt();

// Must be called once at process start (wraps sodium_init()).
bool initLibrary();

} // namespace msg::crypto
