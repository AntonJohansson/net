BUILDDIR := $(abspath build)
CLIENT := $(BUILDDIR)/client
CLIENT_STRESS := $(BUILDDIR)/client-stress
SERVER := $(BUILDDIR)/server

all: $(BUILDDIR) $(SERVER) $(CLIENT) $(CLIENT_STRESS)

$(SERVER): src/server.c
	$(CC) -o $@ $^ -g -O2 -lraylib -lm

$(CLIENT): src/client.c
	$(CC) -o $@ $^ -g -O2 -lraylib -lm

$(CLIENT_STRESS): src/client.c
	$(CC) -o $@ $^ -g -O2 -lm -DSTRESS

$(BUILDDIR):
	mkdir -p $(BUILDDIR)
