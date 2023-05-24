all:
	gcc main.c -framework IOKit -framework CoreFoundation

clean:
	rm -f prog