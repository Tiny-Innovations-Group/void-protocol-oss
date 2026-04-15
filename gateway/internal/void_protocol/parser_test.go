package void_protocol_test

import (
	"bytes"
	"os"
	"path/filepath"
	"runtime"
	"testing"

	void_protocol "github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol"
	"github.com/kaitai-io/kaitai_struct_go_runtime/kaitai"
)

// ---- Canonical golden-vector table (VOID-123) ----

type vectorSpec struct {
	file     string
	ccsdsLen int
	snlpLen  int
}

var goldenVectors = []vectorSpec{
	{"packet_a.bin", 72, 80},
	{"packet_b.bin", 184, 192},
	{"packet_c.bin", 104, 112},
	{"packet_d.bin", 128, 136},
	{"packet_h.bin", 112, 120},
	{"packet_ack.bin", 120, 136},
	{"packet_l.bin", 40, 48},
}

// vectorsDir returns the absolute path of test/vectors at the repo root,
// resolved relative to this test file. Runtime resolution avoids any
// dependency on the caller's working directory.
func vectorsDir(t *testing.T) string {
	t.Helper()
	_, thisFile, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatalf("runtime.Caller failed")
	}
	// parser_test.go lives at gateway/internal/void_protocol/ — walk up
	// three levels to reach the repo root, then into test/vectors.
	return filepath.Join(filepath.Dir(thisFile), "..", "..", "..", "test", "vectors")
}

func readVector(t *testing.T, tier, file string) []byte {
	t.Helper()
	path := filepath.Join(vectorsDir(t), tier, file)
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return data
}

func parseVector(t *testing.T, raw []byte) *void_protocol.VoidProtocol {
	t.Helper()
	stream := kaitai.NewStream(bytes.NewReader(raw))
	p := void_protocol.NewVoidProtocol()
	if err := p.Read(stream, nil, p); err != nil {
		t.Fatalf("kaitai parse failed: %v", err)
	}
	return p
}

// TestGoldenVectorSizes — VOID-114B frame alignment, locked on the Go side.
// For every committed .bin vector, load the bytes, parse via the Kaitai
// VoidProtocol reader, and assert the total length matches SIZE_PACKET_*
// AND satisfies both the 32-bit (%4) and 64-bit (%8) machine-cycle
// alignment rules.
func TestGoldenVectorSizes(t *testing.T) {
	tiers := []struct {
		name   string
		isSnlp bool
	}{
		{"ccsds", false},
		{"snlp", true},
	}

	for _, v := range goldenVectors {
		for _, tier := range tiers {
			want := v.ccsdsLen
			if tier.isSnlp {
				want = v.snlpLen
			}
			t.Run(tier.name+"/"+v.file, func(t *testing.T) {
				raw := readVector(t, tier.name, v.file)
				if got := len(raw); got != want {
					t.Fatalf("%s: got %d bytes, want %d", v.file, got, want)
				}
				if len(raw)%4 != 0 {
					t.Errorf("%s: %d bytes fails %%4==0 (32-bit alignment)", v.file, len(raw))
				}
				if len(raw)%8 != 0 {
					t.Errorf("%s: %d bytes fails %%8==0 (64-bit alignment)", v.file, len(raw))
				}
				if len(raw) > 255 {
					t.Errorf("%s: %d bytes exceeds LoRa 255-byte ceiling", v.file, len(raw))
				}

				// Full kaitai parse must succeed.
				p := parseVector(t, raw)

				// Tier routing must match the subdir the file came from.
				isSnlp, err := p.IsSnlp()
				if err != nil {
					t.Fatalf("IsSnlp: %v", err)
				}
				if isSnlp != tier.isSnlp {
					t.Errorf("tier mismatch: parsed isSnlp=%v, expected %v", isSnlp, tier.isSnlp)
				}
			})
		}
	}
}
