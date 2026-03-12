package main

import (
	"bytes"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
)

func main() {
	// 1. Setup command line flags
	packetName := flag.String("packet", "void_packet_d_delivery_ccsds.bin", "The name of the generated .bin file to inject")
	dirName := flag.String("dir", "generated_packets", "The directory containing the .bin files")
	flag.Parse()

	filePath := filepath.Join(*dirName, *packetName)

	// 2. Read the raw binary file
	binaryData, err := os.ReadFile(filePath)
	if err != nil {
		fmt.Printf("❌ Failed to read packet file '%s': %v\n", filePath, err)
		fmt.Println("Did you run 'python3 gen_packet.py' first?")
		return
	}

	fmt.Printf("🚀 Firing Mock Packet: %s (%d bytes)\n", *packetName, len(binaryData))

	// 3. Send the raw binary to the local Gateway
	url := "http://localhost:8080/api/v1/ingest"
	resp, err := http.Post(url, "application/octet-stream", bytes.NewReader(binaryData))
	if err != nil {
		fmt.Printf("❌ Gateway is down. Did you run 'go run main.go' in another terminal?\nError: %v\n", err)
		return
	}
	defer resp.Body.Close()

	// 4. Print Gateway Response
	bodyBytes, _ := io.ReadAll(resp.Body)
	fmt.Printf("📡 Gateway Status: %s\n", resp.Status)
	fmt.Printf("📡 Gateway Body: %s\n", string(bodyBytes))
}
