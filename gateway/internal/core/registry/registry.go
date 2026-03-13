package registry

import (
	"log"
)

// SatRecord holds the critical identity data for a satellite
type SatRecord struct {
	SatID     uint32
	PubKeyHex string // Ed25519 Public Key
	Wallet    string // Ethereum/L2 Wallet Address
	Role      string // "Seller" or "Mule"
}

// MockDB simulates our NoSQL database
var MockDB = map[uint32]SatRecord{}

// Initialize populates our hardcoded registry
func Initialize() {
	// Example Sat A (The Seller/Gas Station)
	MockDB[100] = SatRecord{
		SatID:     100,
		PubKeyHex: "e7a891c243f0d4b5e678a912c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2", // We will replace this with a real generated key later
		Wallet:    "0x1A2B3C4D5E6F7A8B9C0D1E2F3A4B5C6D7E8F9A0B",
		Role:      "Seller",
	}

	// Example Sat B (The Buyer/Mule)
	MockDB[101] = SatRecord{
		SatID:     101,
		PubKeyHex: "b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5",
		Wallet:    "0x9F8E7D6C5B4A39281706F5E4D3C2B1A09F8E7D6C",
		Role:      "Mule",
	}

	log.Println("🗄️  Registry Initialized with Hardcoded Satellites")
}

func GetSat(id uint32) (SatRecord, bool) {
	sat, exists := MockDB[id]
	return sat, exists
}
