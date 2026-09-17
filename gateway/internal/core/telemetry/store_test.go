package telemetry

import (
	"bufio"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func testRecord(epoch uint64) HeartbeatRecord {
	return HeartbeatRecord{
		ReceivedAtMs: 1781238335000,
		Apid:         101,
		Tier:         "snlp",
		EpochTs:      epoch,
		PressurePa:   100800,
		LatFixed:     515000000,
		LonFixed:     -128000,
		VbattMv:      4100,
		TempC:        2300,
		GpsSpeedCms:  600,
		SysState:     3,
		SatLock:      8,
		RawHex:       "1d01a5a5",
	}
}

func TestAppendCreatesFileAndRoundTripsFields(t *testing.T) {
	path := filepath.Join(t.TempDir(), "data", "heartbeats.json")
	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}

	if err := s.Append(testRecord(1710000100000)); err != nil {
		t.Fatalf("Append: %v", err)
	}

	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read back: %v", err)
	}
	var got HeartbeatRecord
	if err := json.Unmarshal(raw, &got); err != nil {
		t.Fatalf("unmarshal line: %v", err)
	}
	want := testRecord(1710000100000)
	if got != want {
		t.Errorf("round-trip mismatch:\n got %+v\nwant %+v", got, want)
	}
}

func TestAppendIsAppendOnly(t *testing.T) {
	path := filepath.Join(t.TempDir(), "heartbeats.json")
	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}

	for i := uint64(0); i < 3; i++ {
		if err := s.Append(testRecord(1710000100000 + i)); err != nil {
			t.Fatalf("Append %d: %v", i, err)
		}
	}

	f, err := os.Open(path)
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	defer f.Close()

	var lines int
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		var rec HeartbeatRecord
		if err := json.Unmarshal(sc.Bytes(), &rec); err != nil {
			t.Fatalf("line %d not valid JSON: %v", lines, err)
		}
		if rec.EpochTs != 1710000100000+uint64(lines) {
			t.Errorf("line %d out of order: epoch %d", lines, rec.EpochTs)
		}
		lines++
	}
	if lines != 3 {
		t.Errorf("got %d lines, want 3", lines)
	}
}
