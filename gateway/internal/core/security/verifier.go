package security

import (
	"crypto/ed25519"
	"encoding/hex"
	"fmt"
	"log"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/registry"
)

// VerifyPacketSignature checks an Ed25519 signature against the satellite's registered PUF key.
// - satID: The ID extracted from the packet (e.g., 100).
// - message: The raw bytes of the packet payload (excluding the signature itself).
// - signature: The 64-byte signature extracted from the packet.
func VerifyPacketSignature(satID uint32, message []byte, signature []byte) error {

	// 1. Lookup the Satellite in our Database
	satRecord, exists := registry.GetSat(satID)
	if !exists {
		return fmt.Errorf("SECURITY ALERT: Unknown Satellite ID %d. Cannot verify signature", satID)
	}

	// 2. Decode the Public Key from Hex to Raw Bytes
	pubKeyBytes, err := hex.DecodeString(satRecord.PubKeyHex)
	if err != nil {
		return fmt.Errorf("internal error: failed to decode pubkey for sat %d: %v", satID, err)
	}

	// 3. Ensure the key length is correct for Ed25519 (Must be 32 bytes)
	if len(pubKeyBytes) != ed25519.PublicKeySize {
		return fmt.Errorf("internal error: invalid public key length for sat %d", satID)
	}
	pubKey := ed25519.PublicKey(pubKeyBytes)

	// 4. Perform the Cryptographic Verification
	isValid := ed25519.Verify(pubKey, message, signature)

	if !isValid {
		return fmt.Errorf("SECURITY ALERT: Invalid Ed25519 Signature for SatID %d! Possible spoofing attack", satID)
	}

	log.Printf("🔐 Verified: PUF Signature for SatID %d is authentic.", satID)
	return nil
}
