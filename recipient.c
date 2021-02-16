#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

#define PORT 8888
#define IP_ADDR "ip adress" //127.0.0.1
#define ALSA_PCM_NEW_HW_PARAMS_API
#define FRAMES 64
#define SIZE (FRAMES * 4)
#define BUFSIZE 10000
#define LOOP 32

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
bool writeFirst = true;
int somethingSend = true;
char *buffer;
char *buffer2;

void *writeToCard(void *handle)
{
    while(somethingSend){}
    while (1)
    {
        int rc;
        if (pthread_mutex_trylock(&mutex) == 0)
        {
            rc = snd_pcm_writei((snd_pcm_t *)handle, buffer, LOOP * FRAMES);
            pthread_mutex_unlock(&mutex);
            if (rc == -EPIPE)
            {
                fprintf(stderr, "Underrun occured\n");
                snd_pcm_prepare((snd_pcm_t *)handle);
            }
        }

        if (pthread_mutex_trylock(&mutex2) == 0)
        {
            rc = snd_pcm_writei((snd_pcm_t *)handle, buffer2, LOOP * FRAMES);
            pthread_mutex_unlock(&mutex2);
            if (rc == -EPIPE)
            {
                fprintf(stderr, "Underrun occured\n");
                snd_pcm_prepare((snd_pcm_t *)handle);
            }
        }
        else if (rc < 0)

        {
            fprintf(stderr, "Error from writei: %s\n",
                    snd_strerror(rc));
        }
        else if (rc != LOOP * FRAMES)
        {
            fprintf(stderr, "short write, write %d frames\n", rc);
        }
    }
}

void *udpServer(void *socketfd)
{
    puts("Start Servera");
    while (1)
    {
        if (writeFirst)
        {
            pthread_mutex_lock(&mutex);
            for (int i = 0; i < LOOP; i++)
                if (recvfrom(*(int *)socketfd, buffer + i * SIZE, SIZE, 0, NULL, NULL) < 0)
                {
                    perror("recvfrom() ERROR");
                    exit(4);
                }
            //rc = write(1, buffer, LOOP*SIZE); //mozna zapisywac dzwiek do pliku
            writeFirst = false;
            pthread_mutex_unlock(&mutex);
        }
        else
        {
            pthread_mutex_lock(&mutex2);
            for (int i = 0; i < LOOP; i++)
                if (recvfrom(*(int *)socketfd, buffer2 + i * SIZE, SIZE, 0, NULL, NULL) < 0)
                {
                    perror("recvfrom() ERROR");
                    exit(4);
                }
            //rc = write(1, buffer2, LOOP*SIZE);
            writeFirst = true;
            somethingSend = false;
            pthread_mutex_unlock(&mutex2);
        }
    }
}

snd_pcm_t *setAudioParameters(snd_pcm_uframes_t frames)
{
    int rc;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int val;
    int dir;

    rc = snd_pcm_open(&handle, "default", /*hw:0,0  - przekaze to karte na wylacznosc*/
                      SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0)
    {
        fprintf(stderr,
                "unable to open pcm device: %s\n",
                snd_strerror(rc));
        exit(1);
    }
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params,
                                 SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params,
                                 SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, params, 2);
    val = 44100;
    snd_pcm_hw_params_set_rate_near(handle, params,
                                    &val, &dir);
    snd_pcm_hw_params_set_period_size_near(handle,
                                           params, &frames, &dir);
    rc = snd_pcm_hw_params(handle, params);
    if (rc < 0)
    {
        fprintf(stderr,
                "unable to set hw parameters: %s\n",
                snd_strerror(rc));
        exit(1);
    }
    snd_pcm_hw_params_get_period_size(params,
                                      &frames, &dir);
    return handle;
}

int createUDPSocket()
{
    int socketfd;
    struct sockaddr_in servaddr, cliaddr;

    if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Socket creation failed.");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, IP_ADDR, &servaddr.sin_addr) <= 0)
    {
        perror("Couldn't convert IP address to binary.");
        exit(EXIT_FAILURE);
    }

    if (bind(socketfd, (const struct sockaddr *)&servaddr,
             sizeof(servaddr)) < 0)
    {
        perror("Bind failed.");
        exit(EXIT_FAILURE);
    }
    return socketfd;
}

int main()
{
    snd_pcm_t *handle = setAudioParameters(FRAMES);
    buffer = (char *)malloc(BUFSIZE);
    buffer2 = (char *)malloc(BUFSIZE);
    int rc;

    int socketfd = createUDPSocket();

    pthread_t thread_id_udp, thread_id_card;

    /**********ROZPOCZECIE SZEREGOWANIA******************/
    int s;
    struct sched_param scp, scp2;
    scp.__sched_priority = 1;
    scp2.__sched_priority = 10;
    pthread_attr_t attr;
    pthread_attr_t attr2;
    pthread_attr_t *attrp;
    pthread_attr_t *attrp2;
    attrp = &attr;
    attrp2 = &attr2;

    s = pthread_attr_init(&attr2);
    if (s != 0)
        exit(10);
    s = pthread_attr_setinheritsched(attrp2, PTHREAD_EXPLICIT_SCHED);
    if (s != 0)
        exit(11);
    s = pthread_attr_setschedpolicy(attrp2, 0 /*SCHED_FIFO*/);
    if (s != 0)
        exit(12);
    //s = pthread_attr_setschedparam(attrp2, &scp2);
    if (s != 0)
        exit(13);

    s = pthread_attr_init(&attr);
    if (s != 0)
        exit(14);
    s = pthread_attr_setinheritsched(attrp, PTHREAD_EXPLICIT_SCHED);
    if (s != 0)
        exit(15);
    s = pthread_attr_setschedpolicy(attrp, 0 /*SCHED_FIFO*/);
    if (s != 0)
        exit(16);
    //s = pthread_attr_setschedparam(attrp, &scp);
    if (s != 0)
        exit(17);
    /*******KONIEC SZEREGOWANIA******/

    //wiekszy priorytet dla servera
    pthread_create(&thread_id_udp, attrp2 /*NULL*/, udpServer, &socketfd);
    pthread_create(&thread_id_card, attrp /*NULL*/, writeToCard, handle);
    char c;
    while ((c = getchar()) != 'q')
    {
    }

    pthread_cancel(thread_id_card);
    pthread_cancel(thread_id_udp);
    puts("Stop Servera");
    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    free(buffer);
    free(buffer2);
    pthread_attr_destroy(attrp);
    pthread_attr_destroy(attrp2);

    shutdown(socketfd, SHUT_RDWR);
    puts("Do widzenia!");
    return 0;
}