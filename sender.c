#define _GNU_SOURCE
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
#include <sched.h>
#include <sys/wait.h>

#define PORT 8888
#define IP_ADDR "ip adress" //127.0.0.1
#define FRAMES 64
#define SIZE (FRAMES * 4)

char *buffer;
int socketfd;
struct sockaddr_in servaddr;

snd_pcm_t *setAudioParameters(snd_pcm_uframes_t frames)
{
  int rc;
  snd_pcm_t *handle;
  snd_pcm_hw_params_t *params;
  unsigned int val;
  int dir;

  rc = snd_pcm_open(&handle, "default",
                    SND_PCM_STREAM_CAPTURE, 0);
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

  frames = 64;
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
  return handle;
}

void createSocket()
{
  if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    perror("Socket creation failed.");
    exit(EXIT_FAILURE);
  }

  memset(&servaddr, 0, sizeof(servaddr));

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(PORT);

  if (inet_pton(AF_INET, IP_ADDR, &servaddr.sin_addr) <= 0)
  {
    perror("Couldn't convert IP address to binary.");
    exit(EXIT_FAILURE);
  }
}

void *sendData(void *handle)
{
  int rc;
  socklen_t servLen = sizeof(servaddr);

  while (1)
  {
    rc = snd_pcm_readi((snd_pcm_t *)handle, buffer, FRAMES);
    if (rc == -EPIPE)
    {
      fprintf(stderr, "overrun occurred\n");
      snd_pcm_prepare((snd_pcm_t *)handle);
    }
    else if (rc < 0)
    {
      fprintf(stderr,
              "error from read: %s\n",
              snd_strerror(rc));
    }
    else if (rc != FRAMES)
    {
      fprintf(stderr, "short read, read %d frames\n", rc);
    }

    if (sendto(socketfd, buffer, SIZE, 0,
               (const struct sockaddr *)&servaddr,
               servLen) < 0)
    {
      perror("sendto() ERROR");
      exit(1);
    }
  }
}

int main()
{
  int rc;
  snd_pcm_t *handle = setAudioParameters(FRAMES);
  createSocket();

  buffer = (char *)malloc(FRAMES);

  pthread_t thread;

  pthread_create(&thread, NULL, sendData, handle);

  char c;
  while ((c = getchar()) != 'q')
  {
  }
  pthread_cancel(thread);
  shutdown(socketfd, SHUT_RDWR);
  free(buffer);
  printf("Do widzenia!\n");
  return 0;
}