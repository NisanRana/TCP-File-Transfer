# TCP-File-Transfer 

### Source Codes
- client.c
-  server.c

### How to use 
run the following commands in your terminal

-  `gcc -o server server.c`

- `gcc -o client client.c`

- `./server`
 
- `./client`


### note: if client gets error creating "send_file.txt" , create it manually
-  `touch send_file.txt`
-  `echo "Test file" >> send_file.txt`
