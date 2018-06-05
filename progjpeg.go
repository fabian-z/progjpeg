package progjpeg

/*
#include <stdlib.h>
#include <stdint.h>
#cgo LDFLAGS: -ljpeg
#cgo CFLAGS: -Wall -Werror -O3 -fstack-protector-strong -D_FORTIFY_SOURCE=2

void lossless_progressive_jpeg(void *jpg_buffer, uint64_t jpg_size, void **outbuffer, uint64_t *outlen, int errorcb, int successcb);
extern void go_callback_int(int foo, char* p1);
*/
import "C"

import (
	"bytes"
	"fmt"
	"image"
	"image/jpeg"
	"sync"
	"unsafe"
)

func Encode(img image.Image, opt *jpeg.Options) ([]byte, error) {

	buf := new(bytes.Buffer)
	err := jpeg.Encode(buf, img, opt)

	if err != nil {
		return nil, err
	}

	in := buf.Bytes()
	var out unsafe.Pointer
	var outlength C.uint64_t

	returnChan := make(chan error)

	errorCb := func(err *C.char) {
		returnChan <- fmt.Errorf("libjpeg error: %s", C.GoString(err))
	}
	successCb := func(empty *C.char) {
		returnChan <- nil
	}
	errorCbIndex := register(errorCb)
	successCbIndex := register(successCb)

	go func() {
		C.lossless_progressive_jpeg(unsafe.Pointer(&in[0]), C.uint64_t(len(in)), &out, &outlength, C.int(errorCbIndex), C.int(successCbIndex))
	}()

	ret := <-returnChan
	unregister(errorCbIndex, successCbIndex)

	if ret != nil {
		return nil, ret
	}

	outBytes := C.GoBytes(out, C.int(outlength))
	C.free(out)

	return outBytes, nil
}

// https://github.com/golang/go/wiki/cgo#function-variables

//export go_jpeg_callback
func go_jpeg_callback(foo C.int, p1 *C.char) {
	fn := lookup(int(foo))
	fn(p1)
}

var mu sync.Mutex
var index int
var fns = make(map[int]func(*C.char))

func register(fn func(*C.char)) int {
	mu.Lock()
	defer mu.Unlock()
	index++
	for fns[index] != nil {
		index++
	}
	fns[index] = fn
	return index
}

func lookup(i int) func(*C.char) {
	mu.Lock()
	defer mu.Unlock()
	return fns[i]
}

func unregister(i ...int) {
	mu.Lock()
	defer mu.Unlock()
	for _, v := range i {
		delete(fns, v)
	}
}
