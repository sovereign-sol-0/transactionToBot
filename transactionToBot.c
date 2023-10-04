#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <solana-client.h>
#include <telebot.h>

#define TOKEN "PLACE YOUR TELEGRAM BOT TOKEN HERE" // Token del bot Telegram
#define CHAT_ID "PLACE YOUR TELEGRAM CHAT ID HERE" // ID della chat Telegram
#define URL "https://api.mainnet-beta.solana.com" // URL del nodo RPC del validator
#define MAX_ATTEMPTS 3 // Numero massimo di tentativi di connessione
#define INTERVAL 3600 // Intervallo in secondi tra ogni controllo e invio

// Struttura per memorizzare il numero di transazioni per ogni tipo
typedef struct {
int solana;
int spl_token;
int serum;
int stake;
int vote;
int other;
} transaction_counts;

// Variabili globali per il client RPC, il bot Telegram e il timer
solana_client_t *client;
telebot_handler_t *handler;
timer_t timer;

// Funzione per classificare una transazione in base al programma a cui è associata
char *classify_transaction(solana_transaction_t *transaction) {
// Estrae le istruzioni dalla transazione
solana_instruction_t *instructions = transaction->message.instructions;
size_t instruction_count = transaction->message.instruction_count;

// Controlla se la transazione è associata a uno dei programmi noti
for (size_t i = 0; i < instruction_count; i++) {
solana_instruction_t *instruction = &instructions[i];
char *program_id = instruction->program_id;

if (strcmp(program_id, "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA") == 0) {
return "spl-token"; // Transazione SPL Token
} else if (strcmp(program_id, "DESVgJVGajEgKGXhb6XmqDHGz3VjdgP7rEVESBgxmroY") == 0) {
return "serum"; // Transazione Serum
} else if (strcmp(program_id, "Stake11111111111111111111111111111111111111") == 0) {
return "stake"; // Transazione Stake
} else if (strcmp(program_id, "Vote111111111111111111111111111111111111111") == 0) {
return "vote"; // Transazione Vote
}
}

// Se nessuno dei programmi noti è coinvolto, restituisce "solana" come default
return "solana"; // Transazione Solana
}

// Funzione per controllare che tipo di transazioni vengono gestite dal validator e inviarle al bot Telegram
void check_and_send() {
// Crea una struttura per memorizzare il numero di transazioni per ogni tipo
transaction_counts counts = {0};

// Ottiene il numero dell'ultimo slot confermato dal nodo RPC
uint64_t last_slot = solana_client_get_slot(client);

// Ottiene le transazioni confermate nell'ultimo slot dal nodo RPC
solana_transaction_list_t *transactions = solana_client_get_confirmed_signatures_for_slot(client, last_slot);

// Itera sulle transazioni e le classifica usando la funzione definita sopra
for (size_t i = 0; i < transactions->count; i++) {
char *signature = transactions->signatures[i];

// Ottiene i dettagli della transazione dal nodo RPC
solana_transaction_t *transaction = solana_client_get_confirmed_transaction(client, signature);

// Controlla se la transazione è stata eseguita con successo
if (transaction->meta.status == SOLANA_TRANSACTION_STATUS_SUCCESS) {
// Classifica la transazione in base al programma a cui è associata
char *transaction_type = classify_transaction(transaction);

// Incrementa il contatore per il tipo di transazione nella struttura
if (strcmp(transaction_type, "solana") == 0) {
counts.solana++;
} else if (strcmp(transaction_type, "spl-token") == 0) {
counts.spl_token++;
} else if (strcmp(transaction_type, "serum") == 0) {
counts.serum++;
} else if (strcmp(transaction_type, "stake") == 0) {
counts.stake++;
} else if (strcmp(transaction_type, "vote") == 0) {
counts.vote++;
} else {
counts.other++;
}
}

// Libera la memoria della transazione
solana_transaction_free(transaction);
}

// Libera la memoria della lista delle transazioni
solana_transaction_list_free(transactions);

// Crea un messaggio con il numero e la percentuale di transazioni per ogni tipo e il totale
char message[256];
int total = counts.solana + counts.spl_token + counts.serum + counts.stake + counts.vote + counts.other;
sprintf(message, "Transazioni processate nell'ultimo slot: %d\n", total);
sprintf(message + strlen(message), "Solana: %d (%.2f%%)\n", counts.solana, (double)counts.solana / total * 100);
sprintf(message + strlen(message), "SPL Token: %d (%.2f%%)\n", counts.spl_token, (double)counts.spl_token / total * 100);
sprintf(message + strlen(message), "Serum: %d (%.2f%%)\n", counts.serum, (double)counts.serum / total * 100);
sprintf(message + strlen(message), "Stake: %d (%.2f%%)\n", counts.stake, (double)counts.stake / total * 100);
sprintf(message + strlen(message), "Vote: %d (%.2f%%)\n", counts.vote, (double)counts.vote / total * 100);
sprintf(message + strlen(message), "Altro: %d (%.2f%%)\n", counts.other, (double)counts.other / total * 100);

// Invia il messaggio al bot Telegram usando l'API di Telegram
telebot_error_e error = telebot_handler_send_message(handler, CHAT_ID, message, "", false, false, 0, "");
if (error != TELEBOT_ERROR_NONE) {
fprintf(stderr, "Errore nell'invio del messaggio al bot Telegram: %s\n", telebot_error_string(error));
}
}

// Funzione per gestire il segnale di uscita e liberare le risorse
void handle_exit(int signum) {
// Disattiva il timer
timer_delete(timer);

// Disconnette il client RPC e il bot Telegram
solana_client_disconnect(client);
telebot_handler_delete(handler);

// Libera la memoria del client RPC e del bot Telegram
solana_client_free(client);
telebot_delete(handler);

// Termina il programma con lo stato EXIT_SUCCESS
exit(EXIT_SUCCESS);
}

// Funzione principale del programma
int main() {
// Crea un client RPC per connettersi al nodo RPC del validator
client = solana_client_new(URL);

// Crea un bot Telegram per connettersi all'API di Telegram
handler = telebot_new(TOKEN);

// Registra una funzione per gestire il segnale di uscita SIGINT (ctrl-c)
signal(SIGINT, handle_exit);

// Crea una struttura per memorizzare le informazioni sul timer
struct sigevent sev;

// Imposta la funzione da eseguire quando scade il timer
sev.sigev_notify = SIGEV_THREAD;
sev.sigev_notify_function = check_and_send;
sev.sigev_notify_attributes = NULL;
sev.sigev_value.sival_ptr = NULL;

// Crea il timer usando la struttura appena creata
timer_create(CLOCK_REALTIME, &sev, &timer);

// Crea una struttura per memorizzare le informazioni sull'intervallo del timer
struct itimerspec its;

// Imposta l'intervallo del timer in secondi e nanosecondi
its.it_interval.tv_sec = INTERVAL;
its.it_interval.tv_nsec = 0;

// Imposta il primo scadere del timer in secondi

timer_settime(timer, 0, &its, NULL);

•  Entra in un ciclo infinito in cui aspetta i segnali generati dal timer

while (1) {
pause(); // Sospende l'esecuzione fino a quando non arriva un segnale
}
