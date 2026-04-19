package receipt

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"sync"
)

// Record is the JSONL row written to gateway/data/receipts.json. Each
// row carries the full provenance chain for one settled PacketB:
//   - payment_id / settlement_tx_hash — the dedup key (both mandatory)
//   - sat_id / amount / asset_id / wallet — echoed from the PacketB and
//     the registry lookup; redundant with the on-chain record but keeps
//     the audit artefact self-contained for offline review
//   - packet_c_hex — the signed 112-byte PacketC the gateway emitted
//     back to the seller (see builder.go)
//   - block_number / ts_ms — on-chain coordinates of the settlement
//
// Fields intentionally use JSON-compatible types (string for big
// integers, hex for byte arrays) so the file is directly consumable by
// jq / grep / human reviewers.
type Record struct {
	PaymentID        string `json:"payment_id"`
	SettlementTxHash string `json:"settlement_tx_hash"`
	SatID            uint32 `json:"sat_id"`
	Amount           string `json:"amount"` // decimal form of big.Int
	AssetID          uint16 `json:"asset_id"`
	Wallet           string `json:"wallet"`
	PacketCHex       string `json:"packet_c_hex"`
	BlockNumber      uint64 `json:"block_number"`
	TsMs             int64  `json:"ts_ms"`
}

// ErrDuplicate is returned by Store.Append when the (PaymentID,
// SettlementTxHash) pair is already recorded. Callers MUST treat this
// as a "nothing to do" signal — it is the primary guard against
// double-settling across gateway restarts.
var ErrDuplicate = errors.New("receipt: duplicate payment_id + settlement_tx_hash")

// Store is an append-only JSONL persister with an in-memory dedup set.
// Safe for concurrent callers; each Append serialises on an internal
// mutex and fsyncs before returning, so a crash after a successful
// Append cannot lose the line.
type Store struct {
	path string

	mu   sync.Mutex
	file *os.File
	seen map[string]struct{} // key = PaymentID + "|" + SettlementTxHash
}

// NewStore opens (creating if needed) the JSONL file at path, scans
// any pre-existing lines into an in-memory dedup set, and returns a
// Store ready for concurrent Append. The caller owns Close().
//
// Failure to open the file (non-existent parent dir, permission
// issues, corrupt JSON on a line) returns an error rather than
// silently masking the config problem. A corrupt line is reported
// with the line number so the operator can inspect.
func NewStore(path string) (*Store, error) {
	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_RDWR, 0o644)
	if err != nil {
		return nil, fmt.Errorf("receipt: open %s: %w", path, err)
	}
	s := &Store{
		path: path,
		file: f,
		seen: make(map[string]struct{}),
	}
	if err := s.loadDedupSet(); err != nil {
		_ = f.Close()
		return nil, err
	}
	return s, nil
}

// loadDedupSet reads every existing line in the file, decodes each
// into a Record, and populates s.seen. Called once at open time.
// O_RDWR above means we can seek to 0 for reading without reopening.
func (s *Store) loadDedupSet() error {
	if _, err := s.file.Seek(0, 0); err != nil {
		return fmt.Errorf("receipt: seek: %w", err)
	}
	scanner := bufio.NewScanner(s.file)
	// Bump the scanner's buffer so a long JSONL line (packet_c_hex is
	// 224 chars) isn't mis-reported as "too long".
	scanner.Buffer(make([]byte, 0, 4096), 1<<20)
	lineNo := 0
	for scanner.Scan() {
		lineNo++
		var rec Record
		if err := json.Unmarshal(scanner.Bytes(), &rec); err != nil {
			return fmt.Errorf("receipt: %s:%d: malformed JSONL: %w",
				s.path, lineNo, err)
		}
		s.seen[dedupKey(rec.PaymentID, rec.SettlementTxHash)] = struct{}{}
	}
	if err := scanner.Err(); err != nil {
		return fmt.Errorf("receipt: scan %s: %w", s.path, err)
	}
	// Seek back to end so subsequent writes keep the append invariant.
	if _, err := s.file.Seek(0, 2); err != nil {
		return fmt.Errorf("receipt: seek end: %w", err)
	}
	return nil
}

// Has returns true if a record with this (paymentID, txHash) pair has
// already been persisted. Cheap — in-memory map lookup under a short
// mutex critical section. Designed to be called from the orchestrator
// BEFORE running the (relatively expensive) PacketC builder.
func (s *Store) Has(paymentID, txHash string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	_, ok := s.seen[dedupKey(paymentID, txHash)]
	return ok
}

// Count returns the number of records in the dedup set. Useful in
// tests and for a /stats endpoint (future). Takes the mutex, so
// callers should not spin on it.
func (s *Store) Count() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.seen)
}

// Append writes one Record as a single JSONL line to the file,
// fsyncs, and marks the pair as seen. Returns ErrDuplicate if the
// (PaymentID, SettlementTxHash) pair has been recorded previously —
// the file is NOT modified in that case.
func (s *Store) Append(rec Record) error {
	key := dedupKey(rec.PaymentID, rec.SettlementTxHash)

	s.mu.Lock()
	defer s.mu.Unlock()

	if _, dup := s.seen[key]; dup {
		return ErrDuplicate
	}

	line, err := json.Marshal(rec)
	if err != nil {
		return fmt.Errorf("receipt: marshal: %w", err)
	}
	line = append(line, '\n')

	if _, err := s.file.Write(line); err != nil {
		return fmt.Errorf("receipt: write %s: %w", s.path, err)
	}
	if err := s.file.Sync(); err != nil {
		return fmt.Errorf("receipt: fsync %s: %w", s.path, err)
	}
	s.seen[key] = struct{}{}
	return nil
}

// Close flushes and releases the underlying file handle. Safe to call
// multiple times. After Close all Append calls will error.
func (s *Store) Close() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.file == nil {
		return nil
	}
	err := s.file.Close()
	s.file = nil
	return err
}

func dedupKey(paymentID, txHash string) string {
	return paymentID + "|" + txHash
}
