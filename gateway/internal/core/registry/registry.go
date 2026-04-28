package registry

import (
	"crypto/ed25519"
	"encoding/hex"
	"fmt"
	"log"
)

// SatRecord holds the critical identity data for a satellite.
//
// The Wallet field is the seller's EVM recipient address — the same
// value the gateway writes into the on-chain Escrow.SettlementIntent
// when it calls settleBatch (VOID-052 / journey #14). For flat-sat
// the address is hardcoded per sat_id; Phase B replaces this with a
// real registry backed by Ed25519-signed attestations (post-HAB).
type SatRecord struct {
	SatID     uint32
	PubKeyHex string // Ed25519 Public Key
	Wallet    string // Ethereum/L2 Wallet Address (EIP-55 checksummed)
	Role      string // "Seller" or "Mule"
}

// MockDB simulates our NoSQL database
var MockDB = map[uint32]SatRecord{}

// Deterministic flat-sat sat_id and wallet. The sat_id matches the
// golden-vector constant (`detSatId = 0xCAFEBABE` in
// gateway/test/utils/generate_packets.go) and the wallet is Anvil
// default account #1, the same address the Foundry tests bind to
// sat_id 0xCAFEBABE.
const (
	FlatSatDemoSatID  uint32 = 0xCAFEBABE
	FlatSatDemoWallet        = "0x70997970C51812dc3A010C7d01b50e0d17dc79C8"
)

// Initialize populates our hardcoded registry
func Initialize() {
	// Example Sat A (The Seller/Gas Station)
	MockDB[100] = SatRecord{
		SatID:     100,
		PubKeyHex: "4822f97bc9e8746acc9b2401e21db1afba0212657ad4ae8ee9fd16964dd27d97", // We will replace this with a real generated key later
		Wallet:    "0x1A2B3C4D5E6F7A8B9C0D1E2F3A4B5C6D7E8F9A0B",
		Role:      "Seller",
	}

	// Example Sat B (The Buyer/Mule)
	MockDB[101] = SatRecord{
		SatID:     101,
		PubKeyHex: "4102f95e145ccbaea5959f51cdf2e52e31b2de6bc19ae9ba1347f57e31549dd3",
		Wallet:    "0x9F8E7D6C5B4A39281706F5E4D3C2B1A09F8E7D6C",
		Role:      "Mule",
	}

	// Flat-sat deterministic seller (VOID-052). The pubkey is injected
	// by the gateway startup wiring when it loads the demo seed; seed
	// the wallet here so the sat_id → wallet lookup is live even before
	// pubkey injection. Production registries replace this entry with a
	// real attestation.
	MockDB[FlatSatDemoSatID] = SatRecord{
		SatID:     FlatSatDemoSatID,
		PubKeyHex: "", // filled in by demo-key loader
		Wallet:    FlatSatDemoWallet,
		Role:      "Seller",
	}

	log.Println("🗄️  Registry Initialized with Hardcoded Satellites")
}

func GetSat(id uint32) (SatRecord, bool) {
	sat, exists := MockDB[id]
	return sat, exists
}

// LoadFlatSatDemoKey derives the Ed25519 pubkey from the supplied 32-byte
// seed (hex) and writes it into MockDB[FlatSatDemoSatID].PubKeyHex. Mirrors
// the firmware-side detSeed under VOID_ALPHA_PLAINTEXT — the seller and
// buyer Heltecs both sign with this key, so the gateway can verify their
// PacketB signatures against a single registered identity. VOID-121
// (per-sat segregation + PUF-backed attestation) replaces this post-HAB.
func LoadFlatSatDemoKey(seedHex string) error {
	seed, err := hex.DecodeString(seedHex)
	if err != nil {
		return fmt.Errorf("registry: decode flat-sat seed: %w", err)
	}
	if len(seed) != ed25519.SeedSize {
		return fmt.Errorf("registry: flat-sat seed must be %d bytes, got %d",
			ed25519.SeedSize, len(seed))
	}
	pub, ok := ed25519.NewKeyFromSeed(seed).Public().(ed25519.PublicKey)
	if !ok {
		return fmt.Errorf("registry: failed to derive ed25519 pubkey")
	}
	rec, exists := MockDB[FlatSatDemoSatID]
	if !exists {
		return fmt.Errorf("registry: FlatSatDemoSatID 0x%X not seeded — call Initialize() first",
			FlatSatDemoSatID)
	}
	rec.PubKeyHex = hex.EncodeToString(pub)
	MockDB[FlatSatDemoSatID] = rec
	log.Printf("🔑 Flat-sat demo Ed25519 pubkey injected for sat 0x%X", FlatSatDemoSatID)
	return nil
}
