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
//   - dispatch_status — VOID-135b PENDING/DISPATCHED state machine for
//     the LoRa egress to the bouncer. A blank value on reload is
//     normalised to PENDING (handles legacy #15a-era records).
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
	DispatchStatus   string `json:"dispatch_status,omitempty"`
}

// Dispatch-status values. The JSON wire values are the Go constants —
// keep them stable; any change is an audit-artefact format break.
const (
	StatusPending    = "PENDING"
	StatusDispatched = "DISPATCHED"
)

// ErrDuplicate is returned by Store.Append when the (PaymentID,
// SettlementTxHash) pair is already recorded. Callers MUST treat this
// as a "nothing to do" signal — the primary guard against double-
// settling across gateway restarts.
var ErrDuplicate = errors.New("receipt: duplicate payment_id + settlement_tx_hash")

// ErrUnknown is returned by Store.MarkDispatched when the (paymentID,
// txHash) pair does not match any recorded settlement. Callers
// SHOULD NOT retry — it means the ACK arrived before the Append, or
// the bouncer is lying about what it TXed.
var ErrUnknown = errors.New("receipt: unknown payment_id + settlement_tx_hash")

// Store is an append-only JSONL persister with an in-memory record
// cache. Safe for concurrent callers; each write serialises on an
// internal mutex and fsyncs before returning, so a crash after a
// successful Append or MarkDispatched cannot lose the line.
//
// Order matters: records and order are kept in sync so
// PendingRecords returns entries in append order (oldest first —
// fairness for the bouncer's drain loop).
type Store struct {
	path string

	mu      sync.Mutex
	file    *os.File
	records map[string]Record // key = dedupKey, value = latest seen Record
	order   []string          // dedupKeys in first-append order, for fair drain
}

// NewStore opens (creating if needed) the JSONL file at path, scans
// any pre-existing lines, and returns a Store ready for concurrent
// Append. The caller owns Close().
//
// On reload, records without a dispatch_status field are normalised
// to StatusPending — legacy #15a-era lines (written before this state
// machine existed) re-enter the PENDING queue so the gateway re-offers
// them to the bouncer on boot.
func NewStore(path string) (*Store, error) {
	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_RDWR, 0o644)
	if err != nil {
		return nil, fmt.Errorf("receipt: open %s: %w", path, err)
	}
	s := &Store{
		path:    path,
		file:    f,
		records: make(map[string]Record),
		order:   make([]string, 0, 64),
	}
	if err := s.loadRecords(); err != nil {
		_ = f.Close()
		return nil, err
	}
	return s, nil
}

// loadRecords scans every line, takes the LATEST observed state per
// dedup key, and populates s.records + s.order. Append-order is
// captured from the FIRST time a key is seen; subsequent status
// flips do not disturb the drain ordering.
func (s *Store) loadRecords() error {
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
		// Default legacy or missing status to PENDING.
		if rec.DispatchStatus == "" {
			rec.DispatchStatus = StatusPending
		}
		key := dedupKey(rec.PaymentID, rec.SettlementTxHash)
		if _, seen := s.records[key]; !seen {
			s.order = append(s.order, key)
		}
		s.records[key] = rec
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
// ever been persisted (regardless of current DispatchStatus).
func (s *Store) Has(paymentID, txHash string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	_, ok := s.records[dedupKey(paymentID, txHash)]
	return ok
}

// Count returns the number of distinct records.
func (s *Store) Count() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.records)
}

// Append writes one Record as a new JSONL line. Returns ErrDuplicate
// if the dedup key has already been recorded. A blank DispatchStatus
// on the caller's struct is filled in as StatusPending before the
// line hits disk.
func (s *Store) Append(rec Record) error {
	key := dedupKey(rec.PaymentID, rec.SettlementTxHash)

	s.mu.Lock()
	defer s.mu.Unlock()

	if _, dup := s.records[key]; dup {
		return ErrDuplicate
	}
	if rec.DispatchStatus == "" {
		rec.DispatchStatus = StatusPending
	}
	if err := s.writeLineLocked(rec); err != nil {
		return err
	}
	s.records[key] = rec
	s.order = append(s.order, key)
	return nil
}

// MarkDispatched flips the record's status from PENDING to DISPATCHED
// by appending a new JSONL line (append-only invariant preserved).
// Idempotent: calling MarkDispatched on an already-DISPATCHED record
// is a no-op (returns nil) so a bouncer retrying a lost ACK does not
// see an error. Returns ErrUnknown if the dedup key was never seen.
func (s *Store) MarkDispatched(paymentID, txHash string) error {
	key := dedupKey(paymentID, txHash)

	s.mu.Lock()
	defer s.mu.Unlock()

	rec, ok := s.records[key]
	if !ok {
		return ErrUnknown
	}
	if rec.DispatchStatus == StatusDispatched {
		return nil // idempotent replay-safe path
	}
	rec.DispatchStatus = StatusDispatched
	if err := s.writeLineLocked(rec); err != nil {
		return err
	}
	s.records[key] = rec
	return nil
}

// PendingRecords returns up to `limit` records currently in
// PENDING state, in append order (oldest first). The slice is a
// fresh copy — callers may sort / filter it without affecting store
// state. A limit ≤ 0 returns an empty slice.
func (s *Store) PendingRecords(limit int) []Record {
	if limit <= 0 {
		return nil
	}
	s.mu.Lock()
	defer s.mu.Unlock()

	out := make([]Record, 0, limit)
	for _, key := range s.order {
		rec, ok := s.records[key]
		if !ok {
			continue // defensive; records and order should stay in sync
		}
		if rec.DispatchStatus != StatusPending {
			continue
		}
		out = append(out, rec)
		if len(out) >= limit {
			break
		}
	}
	return out
}

// Close flushes and releases the underlying file handle. Safe to call
// multiple times. After Close all write methods will error.
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

// writeLineLocked marshals `rec` as one JSONL line, writes + fsyncs.
// MUST be called with s.mu held. Returns a wrapped error including
// the file path on any I/O failure.
func (s *Store) writeLineLocked(rec Record) error {
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
	return nil
}

func dedupKey(paymentID, txHash string) string {
	return paymentID + "|" + txHash
}