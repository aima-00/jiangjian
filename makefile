chat_server: chat_server.c
	gcc chat_server.c -o chat_server -lpthread
chat_client: chat_client.c
	gcc chat_client.c -o chat_client -lpthread
clean:
	rm -f chat_server chat_client
exeserver: chat_server
	./chat_server 8080
execlient: chat_client
	./chat_client 127.0.0.1 8080