// 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
// Authority: Tiny Innovation Group Ltd | License: Apache 2.0
// File: store.go — VOID-022 heartbeat evidence log (append-only JSONL).
//
// Mirrors the receipts.json pattern: one JSON object per line, append-
// only, anchored beside receipts.json under gateway/data/. The file is
// the ground-segment telemetry record — on the flat-sat it builds the
// TRL-4 evidence base; on the real HAB flight the same log is the
// flight-tracking artefact.
package telemetry

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
)

// HeartbeatRecord is one persisted heartbeat observation. Wire fields
// are copied verbatim from the parsed Packet L body; ReceivedAtMs is
// gateway wall-clock at ingest; RawHex retains the full frame so any
// record can be re-parsed or replayed against the KSY schema later.
type HeartbeatRecord struct {
	ReceivedAtMs int64  `json:"received_at_ms"`
	Apid         int    `json:"apid"` // emitter identity (100 seller / 101 buyer)
	Tier         string `json:"tier"` // "snlp" | "ccsds"
	EpochTs      uint64 `json:"epoch_ts"`
	PressurePa   uint32 `json:"pressure_pa"`
	LatFixed     int32  `json:"lat_fixed"` // degrees * 1e7
	LonFixed     int32  `json:"lon_fixed"` // degrees * 1e7
	VbattMv      uint16 `json:"vbatt_mv"`
	TempC        int16  `json:"temp_c"` // centidegrees C
	GpsSpeedCms  uint16 `json:"gps_speed_cms"`
	SysState     uint8  `json:"sys_state"`
	SatLock      uint8  `json:"sat_lock"`
	RawHex       string `json:"raw_hex"`
}

// Appender lets the ingest handler persist heartbeats without binding
// to the concrete Store — tests inject a mock. Mirrors chain.Enqueuer.
type Appender interface {
	Append(rec HeartbeatRecord) error
}

// Store appends HeartbeatRecords to a JSONL file. Safe for concurrent
// use. Unlike receipt.Store there is no state machine and no reload —
// heartbeats are pure append-only evidence with no dedup semantics.
type Store struct {
	mu   sync.Mutex
	path string
}

// NewStore creates (if needed) the parent directory and returns a
// Store targeting path. The file itself is created lazily on first
// Append so an idle gateway leaves no empty artefact.
func NewStore(path string) (*Store, error) {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return nil, err
	}
	return &Store{path: path}, nil
}

// Append writes one JSONL line and fsyncs. At the 30-second heartbeat
// cadence the open-write-sync-close cycle is negligible and keeps the
// file crash-consistent without holding a descriptor open.
func (s *Store) Append(rec HeartbeatRecord) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	b, err := json.Marshal(rec)
	if err != nil {
		return err
	}
	f, err := os.OpenFile(s.path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	if _, err := f.Write(append(b, '\n')); err != nil {
		return err
	}
	return f.Sync()
}
