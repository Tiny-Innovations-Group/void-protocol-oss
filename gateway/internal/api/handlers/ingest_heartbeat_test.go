package handlers_test

// VOID-022: heartbeat ingest — CRC gate + evidence persistence. Uses
// the checked-in golden vectors (VOID-123) and the shared helpers from
// ingest_test.go. The package-level handlers.Heartbeats sink is swapped
// per test and restored, mirroring how ingest_chain_test.go treats
// handlers.Submitter.

import (
	"encoding/hex"
	"net/http"
	"testing"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/telemetry"
)

type mockHeartbeatSink struct {
	recs []telemetry.HeartbeatRecord
}

func (m *mockHeartbeatSink) Append(rec telemetry.HeartbeatRecord) error {
	m.recs = append(m.recs, rec)
	return nil
}

func TestIngestHeartbeatPersistsEvidence(t *testing.T) {
	r := newIngestRouter()
	sink := &mockHeartbeatSink{}
	handlers.Heartbeats = sink
	defer func() { handlers.Heartbeats = nil }()

	raw := loadGoldenVector(t, "snlp", "packet_l.bin")
	if len(raw) != 48 {
		t.Fatalf("expected 48-byte SNLP packet_l, got %d", len(raw))
	}

	w := postIngest(t, r, raw)
	if w.Code != http.StatusOK {
		t.Fatalf("golden heartbeat: got %d, want 200. body=%s", w.Code, w.Body.String())
	}
	if len(sink.recs) != 1 {
		t.Fatalf("got %d persisted records, want 1", len(sink.recs))
	}

	rec := sink.recs[0]
	if rec.Apid != 101 {
		t.Errorf("apid = %d, want 101 (golden apidSatB)", rec.Apid)
	}
	if rec.Tier != "snlp" {
		t.Errorf("tier = %q, want snlp", rec.Tier)
	}
	if rec.EpochTs != 1710000100000 {
		t.Errorf("epoch_ts = %d, want detEpochTsMs", rec.EpochTs)
	}
	if rec.VbattMv != 4100 || rec.TempC != 2300 || rec.GpsSpeedCms != 600 {
		t.Errorf("telemetry fields drifted: vbatt=%d temp=%d speed=%d",
			rec.VbattMv, rec.TempC, rec.GpsSpeedCms)
	}
	if rec.SysState != 3 || rec.SatLock != 8 {
		t.Errorf("state fields drifted: sys_state=%d sat_lock=%d", rec.SysState, rec.SatLock)
	}
	if rec.RawHex != hex.EncodeToString(raw) {
		t.Errorf("raw_hex does not round-trip the ingested frame")
	}
	if rec.ReceivedAtMs == 0 {
		t.Errorf("received_at_ms not stamped")
	}
}

func TestIngestHeartbeatRejectsCRCFlip(t *testing.T) {
	r := newIngestRouter()
	sink := &mockHeartbeatSink{}
	handlers.Heartbeats = sink
	defer func() { handlers.Heartbeats = nil }()

	raw := loadGoldenVector(t, "snlp", "packet_l.bin")
	flipped := append([]byte{}, raw...)
	flipped[20] ^= 0x01 // inside the CRC-covered body (epoch_ts byte)

	w := postIngest(t, r, flipped)
	if w.Code != http.StatusBadRequest {
		t.Fatalf("flipped heartbeat: got %d, want 400. body=%s", w.Code, w.Body.String())
	}
	if len(sink.recs) != 0 {
		t.Fatalf("CRC-fail heartbeat must not be persisted; got %d records", len(sink.recs))
	}
}

func TestIngestHeartbeatWithoutSinkStillAccepts(t *testing.T) {
	r := newIngestRouter()
	handlers.Heartbeats = nil

	raw := loadGoldenVector(t, "snlp", "packet_l.bin")
	w := postIngest(t, r, raw)
	if w.Code != http.StatusOK {
		t.Fatalf("nil sink: got %d, want 200 (persistence is optional). body=%s",
			w.Code, w.Body.String())
	}
}
