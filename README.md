In order to use this project , you need:
     a machine that will be used as a server
     two or three machines that will be used as clients
the machines must have "Bridge Replicate physical network connection state" as the network adapter, and all machines have to be connected to same wifi (all of them must be in the same sub-network)
then in the machines that will be used as a server, create serveur.c, vols.txt, histo.txt, and facture.txt
then in the other machines you only need to create client.c and make sure that the ip adress in the client.c code is the ip adress of the server machine.
then, run serveur.c, you will see that it is listening on port 8080, then in the clients machine, run client.c, and the client will be connected to the server and he can:
    see all flights
    book a flight
    cancel a flight
    see bill
