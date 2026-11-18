package main

import (
	"crypto/sha256"
	"fmt"
	"math"
	"os"
	"runtime"
	"sort"
	"sync"
	"time"
)

func main() {
	fmt.Println("go", runtime.GOARCH, runtime.NumCPU() > 0)
	var wg sync.WaitGroup
	var mu sync.Mutex
	total := 0
	ch := make(chan int, 16)
	for g := 0; g < 8; g++ {
		wg.Add(1)
		go func(g int) {
			defer wg.Done()
			s := 0
			for i := 0; i < 200000; i++ {
				s += i % (g + 2)
			}
			mu.Lock()
			total += s
			mu.Unlock()
			ch <- g
		}(g)
	}
	wg.Wait()
	close(ch)
	n := 0
	for range ch {
		n++
	}
	fmt.Println("goroutines:", n, "total:", total)
	xs := make([]float64, 100000)
	for i := range xs {
		xs[i] = math.Sin(float64(i)) * math.Sqrt(float64(i+1))
	}
	sort.Float64s(xs)
	fmt.Printf("sorted: %.6f %.6f\n", xs[0], xs[len(xs)-1])
	fmt.Printf("sha256: %x\n", sha256.Sum256([]byte("ish64")))
	big := make([][]byte, 0)
	for i := 0; i < 64; i++ {
		big = append(big, make([]byte, 1<<20))
	}
	runtime.GC()
	t := time.Now()
	time.Sleep(20 * time.Millisecond)
	fmt.Println("slept ok:", time.Since(t) >= 20*time.Millisecond, "allocated MB:", len(big))
	m := map[string]int{}
	for i := 0; i < 50000; i++ {
		m[fmt.Sprint(i%977)]++
	}
	fmt.Println("map:", len(m), m["5"])
	defer func() { fmt.Println("recovered:", recover() != nil); os.Exit(0) }()
	var p *int
	_ = *p
}
