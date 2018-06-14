// Début import des librairies //
#include <Arduino.h>
#include <HardwareSerial.h>
#include <RH_RF95.h>
#include <stdint.h>
#include <SoftwareSerial.h>
#include <WString.h>
#include "Wire.h"
#include "I2Cdev.h"
#include "MPU6050.h"
//Fin import des librairies //

#define GYRO_DEBUG false // on utilise pas le module de débuguage inclu avec le gyroscope

SoftwareSerial ss(5, 6); // pin au quelle est branché la carte wifi 
RH_RF95 rf95(ss); // appelle class de la carte wifi et des pins  

int led = 13; // initialisation  de la LED de l'arduino 

MPU6050 accelgyro; // appelle du gyroscope

int16_t ax, ay, az; // création des variable pour les axes du gyroscope
int16_t gx, gy, gz;
int16_t x, y, z;

bool blinkState = false; //

void setup() {// 
	/*
	 * Arduino Init
	 */
	Wire.begin(); //
	Serial.begin(115200); // on initialise la connexion série avec 115200 baud 
	pinMode(led, OUTPUT); // sortie de la led initialisée précédement 

	/*
	 * Gyroscope  & Réseau 
	 */
	Serial.println("Initializing I2C devices..."); // message dans  la console 
	accelgyro.initialize(); // initialisation de acceleromètre et du gyroscope

	Serial.println("Testing device connections..."); // message dans  la console
	Serial.println(accelgyro.testConnection() ? "MPU6050 connection successful" : "MPU6050 connection failed");

	if (!rf95.init()) { // si le module rf95 (HF ) n'est pa s initialisé alors on retourne une erreur
		Serial.println("init failed"); // message d'echec
		while (1) {
			;
		}
	} else {
		Serial.println("init ok"); // sinon le module est initialisé
	}

	rf95.setFrequency(434.0); // on regle la fréquence du module Haute Fréquence a 434 MHz 
}

void loop() {
	listenSerialInterface(); // on attent une commande dans l'entré du moniteur serie 
	accelgyro.getMotion9(&ax, &ay, &az, &gx, &gy, &gz, &x, &y, &z); // on récupere les nombres des axes puis on les stock dans des adresses mémoirs

	if (GYRO_DEBUG) { // Débuguage du gyroscope dans la console 
		Serial.print("a/g:\t"); // on affiche l'orientation 
		Serial.print(x);		// la valeur en x 
		Serial.print("\t");		// tabulation
		Serial.print(y);		// la valeur en y
		Serial.print("\t");		// tabulation
		Serial.println(z);		//la valeur en z
	}

	uint8_t buffer[RH_RF95_MAX_MESSAGE_LEN]; // initialisation d'un buffer contenant la taille maximum qui peut etre recu pas le module HF 
	uint8_t length = sizeof(buffer);		 //recupération de la taille du buffer 

	if (rf95.waitAvailableTimeout(4000)) { // on attent pendant un maximum de 4000ms pour voir si il y a des informations disponiple 
		if (rf95.recv(buffer, &length)) { // si 
			Serial.print("[RadioNetworking] [Client] Received: "); // on affiche dans le moniteur série 
			Serial.println((char*) buffer); // affichage de la conversion en pointer vers d'un buffer
		} else { // sinon  
			Serial.println("[RadioNetworking] [Server] Failed receiving packet"); // on affiche dans la console que la réception a échouée
		}
	} else { // sinon 
		Serial.println("[RadioNetworking] [Client] No reply, is rf95_server running?"); // on affiche dans la console qu'il n'y a aucune réponse . 
	}
}

void listenSerialInterface() { // fonction de détection de commande dans la console 
	while (true) { // quand c'est vrai (boucle infi) 
		if (Serial.available() != 0) { // si il y a des caracteres d'entrés on sort de la boucle 
			break;
		}
	}
	String line = ""; // on se prépart a stocker une variable
	while (Serial.available() != 0) { // quand un caractere est disponiple
		char charectere = Serial.read(); // on lit les caractere recu 
		if (charectere == ';') { // si les caractères contiennent un ; on sort de la boucle 
			break;
		}
		line = line + charectere; // on ajoute les caracteres dans l'espace que l'on a réserver
	}

	if (line == "c") { // si on envoit la commance c (pour caméra ) 
		uint8_t data[] = "c:x____,y____*******"; // on réserve de la place car on ecrit une array caractere par caractere

		String xString = String(x); // on converti les valeurs du gyroscope en string (axe X)
		String yString = String(y); // on converti les valeurs du gyroscope en string (axe Y)

		for (int i = 0; i < min(4, xString.length()); i++) { // on remplace dans la variable uint8_t a partir du 3 eme caracteres 
			data[3 + i] = (char) xString[i];
		}
		for (int i = 0; i < min(4, yString.length()); i++) { // on remplace dans la variable uint8_t a partir du 9 eme caracteres
			data[9 + i] = (char) yString[i];
		}

		rf95.send(data, sizeof(data)); // on envoit (via le module HF) les valeurs du gyro et on precise la taille du buffer 
		rf95.waitPacketSent(); // appelle de la fonction pour attendre la fin de la  transmition des packets 

		Serial.print("[RadioNetworking] [Client] Camera protocol with data: x=");
		Serial.print(xString); // on affiche dans le moniteur série les valeurs de x 
		Serial.print(", y=");
		Serial.println(yString); // on affiche dans le moniteur série les valeurs de y
		Serial.print("[RadioNetworking] [Client] Sending: ");
		Serial.println((char *)data); // affichage de la conversion en pointer vers d'un buffer
	} else if (line == "r") { // commande de rotation du moteur (r)
		uint8_t data[] = "r:a____*************"; // on réserve de la place car on ecrit une array caractere par caractere

		String angleString = String(abs(z)); 

		for (int i = 0; i < min(4, angleString.length()); i++) {
			data[3 + i] = (char) angleString[i];
		}

		rf95.send(data, sizeof(data));
		rf95.waitPacketSent();

		Serial.print("[RadioNetworking] [Client] Rotation protocol with data: angle=");
		Serial.println(angleString);
		Serial.print("[RadioNetworking] [Client] Sending: ");
		Serial.println((char*) data);// affichage de la conversion en pointer vers d'un buffer
	} else if (line == "a") {
		uint8_t data[] = "a:d_,s____**********";

    //String speedString = String(z);

    String speedString = "255"; // 

		data[3] = '1';

		for (int i = 0; i < min(4, speedString.length()); i++) {
			data[6 + i] = (char) speedString[i];
		}

		rf95.send(data, sizeof(data)); // on envoit (via le module HF) les valeurs du gyro et on precise la taille du buffer
		rf95.waitPacketSent(); // appelle de la fonction pour attendre la fin de la  transmition des packets 


		Serial.print("[RadioNetworking] [Client] Action protocol with data: direction=1, speed=");
		Serial.println(speedString);
		Serial.print("[RadioNetworking] [Client] Sending: ");
		Serial.println((char*) data); // affichage de la conversion en pointer vers d'un buffer
	} else { // sinon 
		uint8_t data[] = "************"; // on réserve de la place car on ecrit une array caractere par caractere

		data[0] = line[0]; // si on recoit une commande inconue , on retourne la lettre qui ne l'est pas 
		rf95.send(data, sizeof(data)); // on envoit (via le module HF) les valeurs du gyro et on precise la taille du buffer
		rf95.waitPacketSent();		   // appelle de la fonction pour attendre la fin de la  transmition des packets

		Serial.println("[RadioNetworking] [Client] Unknown protocol with data: no data");
		Serial.print("[RadioNetworking] [Client] Sending: ");
		Serial.println((char*) data);// affichage de la conversion en pointer vers d'un buffer
	}

}
