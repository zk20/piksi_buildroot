TARGET=endpoint_adapter
SOURCES= \
	endpoint_adapter.c \
	endpoint_adapter_stdio.c \
	endpoint_adapter_file.c \
	endpoint_adapter_tcp_listen.c \
	endpoint_adapter_tcp_connect.c \
	endpoint_adapter_udp_listen.c \
	endpoint_adapter_udp_connect.c \
	endpoint_adapter_can.c

LIBS=-luv -lsbp -lpiksi -ldl -lsettings -lpthread
CFLAGS=-std=gnu11 -Wall -ggdb3 -O3

CROSS=

CC=$(CROSS)gcc

all: program
program: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LIBS)

clean:
	rm -rf $(TARGET)
