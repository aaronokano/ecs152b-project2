all: myproxy

myproxy: myproxy.c
	gcc -g -Wall -o myproxy myproxy.c

clean:
	rm -f myproxy
