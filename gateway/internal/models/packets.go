package models

// InnerInvoice represents the 62-byte decrypted payload from Sat A
type InnerInvoice struct {
	EpochTS uint64 `json:"epoch_ts"`
	SatID   uint32 `json:"sat_id"`
	Amount  uint64 `json:"amount"`
	AssetID uint16 `json:"asset_id"`
	// We will map the rest later, keeping it simple for now
}
