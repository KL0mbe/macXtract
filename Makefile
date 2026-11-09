CC = clang
CFLAGS = -O2 -arch arm64 -arch x86_64 -mmacosx-version-min=11.0
LDFLAGS = -framework Carbon

macxtract: macxtract.c
	$(CC) $(CFLAGS) macxtract.c -o macxtract $(LDFLAGS)

zip: macxtract
	zip macxtract.zip macxtract

clean:
	rm -rf macxtract macxtract.zip *.dSYM

run: macxtract
	./macxtract

.PHONY: zip clean run
