
#include <cstdlib>
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/select.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#include <RF24/RF24.h>

#include "PL1167_nRF24.h"
#include "MiLightRadio.h"

RF24 radio(RPI_V2_GPIO_P1_22, RPI_V2_GPIO_P1_24, BCM2835_SPI_SPEED_1MHZ);

PL1167_nRF24 prf(radio);
MiLightRadio mlr(prf);

static int debug = 0;

static int dupesPrinted = 0;

static int maxRemotes = 4;

void receive()
{
  while(1){
    if(mlr.available()) {
      printf("\n");
      uint8_t packet[7];
      size_t packet_length = sizeof(packet);
      mlr.read(packet, packet_length);

      for(size_t i = 0; i < packet_length; i++) {
        printf("%02X ", packet[i]);
        fflush(stdout);
      }
    }

    int dupesReceived = mlr.dupesReceived();
    for (; dupesPrinted < dupesReceived; dupesPrinted++) {
      printf(".");
    }
    fflush(stdout);
  }
}

void send(uint8_t data[8])
{
  static uint8_t seq = 1;

  uint8_t resends = data[7];
  if(data[6] == 0x00){
    data[6] = seq;
    seq++;
  }

  if(debug){
    printf("2.4GHz --> Sending: ");
    for (int i = 0; i < 7; i++) {
      printf("%02X ", data[i]);
    }
    printf(" [x%d]\n", resends);
  }

  mlr.write(data, 7);

  for(int i = 0; i < resends; i++){
    mlr.resend();
  }

}

void send(uint64_t v)
{
  uint8_t data[8];
  data[7] = (v >> (7*8)) & 0xFF;
  data[0] = (v >> (6*8)) & 0xFF;
  data[1] = (v >> (5*8)) & 0xFF;
  data[2] = (v >> (4*8)) & 0xFF;
  data[3] = (v >> (3*8)) & 0xFF;
  data[4] = (v >> (2*8)) & 0xFF;
  data[5] = (v >> (1*8)) & 0xFF;
  data[6] = (v >> (0*8)) & 0xFF;

  send(data);
}

void send(uint8_t color, uint8_t bright, uint8_t key,
          uint16_t remote = 0x0001,
	  uint8_t prefix = 0xB8, uint8_t seq = 0x00, uint8_t resends = 10)
{
  uint8_t data[8];
  data[0] = prefix;
  data[1] = (remote >> 8);
  data[2] = remote & 0xff;
  data[3] = color;
  data[4] = bright;
  data[5] = key;
  data[6] = seq;
  data[7] = resends;

  send(data);
}

double getTime()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + ((double)tv.tv_usec) * 1e-6;
}

void fade(uint8_t prefix, uint16_t remote, uint8_t color, uint8_t bright, uint8_t resends)
{
  uint8_t data[8];
  data[0] = prefix; // prefix
  data[1] = (remote >> 8); // remote byte 1
  data[2] = remote & 0xff; // remote byte 2
  data[4] = 0x00; // bright
  data[5] = 0x0F; // key
  data[6] = 0x00; // seq
  data[7] = resends; // resends

  while(1){
    color++;
    data[3] = color; // color
    send(data);
    usleep(20000);
  }
}

void strobe(uint8_t prefix, uint16_t remote, uint8_t bright, uint8_t resends)
{
  uint8_t data[8];
  data[0] = prefix; // prefix
  data[1] = (remote >> 8); // remote byte 1
  data[2] = remote & 0xff; // remote byte 2
  data[4] = bright; // bright
  data[5] = 0x0F; // key
  data[6] = 0x00; // seq
  data[7] = resends; // resends

  while(1){
    uint8_t color = rand() % 255;
    data[3] = color; // color
    send(data);
    usleep(50000);
  }
}

void udp_raw(uint16_t udp_port)
{
  int sockfd;
  struct sockaddr_in servaddr, cliaddr;
  char mesg[42];

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(udp_port);
  bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

  while(1){
    socklen_t len = sizeof(cliaddr);
    int n = recvfrom(sockfd, mesg, 41, 0, (struct sockaddr *)&cliaddr, &len);

    mesg[n] = '\0';

    if(n == 8){
      if(debug){
        printf("UDP --> Received hex value\n");
      }
      uint8_t data[8];
      for(int i = 0; i < 8; i++){
        data[i] = (uint8_t)mesg[i];
      }
      send(data);
    }
    else {
      fprintf(stderr, "Message has invalid size %d (expecting 8)!\n", n);
    }
  }
}

void udp_milight(uint16_t udp_ports[], uint16_t remotes[], uint8_t resends, uint8_t numRemotes)
{
  fd_set socks;
  int discover_fd, data_fd[maxRemotes];
  struct sockaddr_in discover_addr, data_addr[maxRemotes], cliaddr;
  char mesg[42];
  char reply[30] = "192.168.1.12,BABECAFEBABE,";

  int disco = -1;

  uint8_t data[maxRemotes][8];

  discover_fd = socket(AF_INET, SOCK_DGRAM, 0);
  bzero(&discover_addr, sizeof(discover_addr));
  discover_addr.sin_family = AF_INET;
  discover_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  discover_addr.sin_port = htons(48899);
  bind(discover_fd, (struct sockaddr *)&discover_addr, sizeof(discover_addr));

  for ( unsigned int i = 0; i < numRemotes; i++ )
  {
    data[i][0] = 0xB8; // Disco step
    data[i][1] = (remotes[i] >> 8); // remote byte 1
    data[i][2] = remotes[i] & 0xff; // remote byte 2
    data[i][3] = 0x00; // color
    data[i][4] = 0x00; // bright
    data[i][5] = 0x00; // key
    data[i][6] = 0x01; // Sequence
    data[i][7] = resends; // retries

    data_fd[i] = socket(AF_INET, SOCK_DGRAM, 0);
    bzero(&data_addr[i], sizeof(data_addr[i]));
    data_addr[i].sin_family = AF_INET;
    data_addr[i].sin_addr.s_addr = htonl(INADDR_ANY);
    data_addr[i].sin_port = htons(udp_ports[i]);
    bind(data_fd[i], (struct sockaddr *)&data_addr[i], sizeof(data_addr[i]));
    printf("MilightBridge: Remote ID: %X, Listening on Port %u\n", remotes[i], udp_ports[i]);
  }

  /*
   * The worst hack ever, but probably slightly better than hardcoded
   * Should move this to an ioctl command as there seems to be no better
   * of simpler option to retrieve the IP and MAC.
   */
  if(0){
    FILE *fd;
    size_t s1, s2;
    fd = popen("ifconfig | grep \"inet addr\" | cut -d ':' -f 2 | cut -d ' ' -f 1 | grep -v \"127.0.0.1\" | head -n 1 | tr -d [:space:]", "r");
    s1 = fread(reply, 1, 15, fd);
    reply[s1] = ',';
    s1++;
    fd = popen("ifconfig | grep \"HWaddr\" | cut -d ' ' -f 11 | tr -d [:space:] | tr -d ':' | tr [:lower:] [:upper:]", "r");
    s2 = fread(reply + s1, 1, 12 ,fd);
    reply[s1 + s2] = ',';
    s2++;
    reply[s1 + s2] = '\0';
  }

  if(debug){
    printf("Reply String: %s\n", reply);
    fflush(stdout);
  }

  while(1){
    socklen_t len = sizeof(cliaddr);

    FD_ZERO(&socks);
    FD_SET(discover_fd, &socks);
    for (unsigned int i=0;i<numRemotes;i++)
    {
      FD_SET(data_fd[i], &socks);
    }

    if(select(FD_SETSIZE, &socks, NULL, NULL, NULL) >= 0){

      if(FD_ISSET(discover_fd, &socks)){
        int n = recvfrom(discover_fd, mesg, 41, 0, (struct sockaddr *)&cliaddr, &len);
        mesg[n] = '\0';

        if(debug){
          char str[INET_ADDRSTRLEN];
          long ip = cliaddr.sin_addr.s_addr;
          inet_ntop(AF_INET, &ip, str, INET_ADDRSTRLEN);
          printf("UDP --> Received discovery request (%s) from %s\n", mesg, str);
        }

        if(!strncmp(mesg, "Link_Wi-Fi", 41)){
          sendto(discover_fd, reply, 30, 0, (struct sockaddr*)&cliaddr, len);
        }
      }

      for (unsigned int i=0;i<numRemotes;i++)
      {
        if(FD_ISSET(data_fd[i], &socks)){
          int n = recvfrom(data_fd[i], mesg, 41, 0, (struct sockaddr *)&cliaddr, &len);

          mesg[n] = '\0';

          if(n == 2 || n == 3){
            if(debug){
              printf("UDP --> Received hex value (%02x, %02x, %02x)\n", mesg[0], mesg[1], mesg[2]);
            }

            switch(mesg[0]){
              /* Color */
              case 0x40:
                disco = -1;
                data[i][5] = 0x0F;
                data[i][3] = (0xC8 - mesg[1] + 0x100) & 0xFF;
                data[i][0] = 0xB0;
                break;
              /* All Off */
              case 0x41:
                data[i][5] = 0x02;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* All On */
              case 0x42:
                data[i][4] = (data[i][4] & 0xF8);
                data[i][5] = 0x01;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Disco slower */
              case 0x43:
                data[i][5] = 0x0C;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Disco faster */
              case 0x44:
                data[i][5] = 0x0B;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Z1 On */
              case 0x45:
                data[i][4] = (data[i][4] & 0xF8) | 0x01;
                data[i][5] = 0x03;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Z1 Off */
              case 0x46:
                data[i][5] = 0x04;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Z2 On */
              case 0x47:
                data[i][4] = (data[i][4] & 0xF8) | 0x02;
                data[i][5] = 0x05;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Z2 Off */
              case 0x48:
                data[i][5] = 0x06;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Z3 On */
              case 0x49:
                data[i][4] = (data[i][4] & 0xF8) | 0x03;
                data[i][5] = 0x07;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Z3 Off */
              case 0x4A:
                data[i][5] = 0x08;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
               break;
              /* Z4 On */
              case 0x4B:
                data[i][4] = (data[i][4] & 0xF8) | 0x04;
                data[i][5] = 0x09;
                 if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Z4 Off */
              case 0x4C:
                data[i][5] = 0x0A;
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* Disco */
              case 0x4D:
                disco = (disco + 1) % 9;
                data[i][0] = 0xB0 + disco;
                data[i][5] = 0x0D;
                break;
              /* Brightness */
              case 0x4E:
                data[i][5] = 0x0E;
                data[i][4] = ((0x90 - (mesg[1] * 8) + 0x100) & 0xFF) | (data[i][4] & 0x07);
                if(disco > 0){
                  data[i][0] = 0xB0 + disco;
                }
                break;
              /* All White */
              case 0xC2:
                disco = -1;
                data[i][5] = 0x11;
                break;
              /* Z1 White. */
              case 0xC5:
                disco = -1;
                data[i][5] = 0x13;
                break;
              /* Z2 White. */
              case 0xC7:
                disco = -1;
                data[i][5] = 0x15;
                break;
              /* Z3 White. */
              case 0xC9:
                disco = -1;
                data[i][5] = 0x17;
                break;
              /* Z4 White. */
              case 0xCB:
                disco = -1;
                data[i][5] = 0x19;
                break;
              /* All Night */
              case 0xC1:
                disco = -1;
                data[i][5] = 0x12;
                break;
              /* Z1 Night */
              case 0xC6:
                disco = -1;
                data[i][5] = 0x14;
                break;
              /* Z2 Night */
              case 0xC8:
                disco = -1;
                data[i][5] = 0x16;
                break;
              /* Z3 Night */
              case 0xCA:
                disco = -1;
                data[i][5] = 0x18;
                break;
              /* Z4 Night */
              case 0xCC:
                disco = -1;
                data[i][5] = 0x1A;
                break;
              default:
                fprintf(stderr, "Unknown command %02x!\n", mesg[0]);
                continue;
            } /* End case command */

            /* Send command */
            send(data[i]);
            data[i][6]++;
          }
          else {
            fprintf(stderr, "Message has invalid size %d (expecting 2 or 3)!\n", n);
          } /* End message size check */
        } /* End handling data */
      } /* End loop */
    } /* End select */

  } /* While (1) */

}

void usage(const char *arg, const char *options){
  printf("\n");
  printf("Usage: sudo %s [%s]\n", arg, options);
  printf("\n");
  printf("   -h                       Show this help\n");
  printf("   -d                       Show debug output\n");
  printf("   -f                       Fade mode\n");
  printf("   -s                       Strobe mode\n");
  printf("   -l                       Listening (receiving) mode\n");
  printf("   -u                       UDP mode (raw)\n");
  printf("   -m                       UDP mode (milight)\n");
  printf("   -n NN<dec>               Resends of the same message\n");
  printf("   -p PP<hex>               Prefix value (Disco Mode)\n");
  printf("   -1|2|3|4 RRRR<hex>       Two byte code of the remote\n");
  printf("   -c CC<hex>               Color byte\n");
  printf("   -b BB<hex>               Brightness byte\n");
  printf("   -k KK<hex>               Key byte\n");
  printf("   -v SS<hex>               Sequence byte\n");
  printf("   -w SSPPRRRRCCBBKKNN<hex> Complete message to send\n");
  printf("\n");
  printf(" Author: Roy Bakker (2015)\n");
  printf("\n");
  printf(" Inspired by sources from: - https://github.com/henryk/\n");
  printf("                           - http://torsten-traenkner.de/wissen/smarthome/openmilight.php\n");
  printf("\n");
}

int main(int argc, char** argv)
{
  int do_receive = 0;
  int do_udp     = 0;
  int do_milight = 0;
  int do_strobe  = 0;
  int do_fade    = 0;
  int do_command = 0;

  uint8_t prefix   = 0xB8;
  uint16_t remotes[maxRemotes];
  remotes[0]  = 0x0001;
  remotes[1]  = 0x0002;
  remotes[2]  = 0x0003;
  remotes[3]  = 0x0004;
  uint8_t color    = 0x00;
  uint8_t bright   = 0x00;
  uint8_t key      = 0x01;
  uint8_t seq      = 0x00;
  uint8_t resends  =   10;

  uint64_t command = 0x00;

  uint16_t udp_port = 8899;

  int c;

  uint64_t tmp;

  const char *options = "hdfslumn:p:1:2:3:4:c:b:k:v:w:";

  while((c = getopt(argc, argv, options)) != -1){
    switch(c){
      case 'h':
        usage(argv[0], options);
        exit(0);
        break;
      case 'd':
        debug = 1;
        break;
      case 'f':
        do_fade = 1;
       break;
      case 's':
        do_strobe = 1;
       break;
      case 'l':
        do_receive = 1;
       break;
      case 'u':
        do_udp = 1;
       break;
      case 'm':
        do_milight = 1;
       break;
      case 'n':
        tmp = strtoll(optarg, NULL, 10);
        resends = (uint8_t)tmp;
        break;
      case 'p':
        tmp = strtoll(optarg, NULL, 16);
        prefix = (uint8_t)tmp;
        break;
      case '1':
        tmp = strtoll(optarg, NULL, 16);
        remotes[0] = (uint16_t)tmp;
        break;
      case '2':
        tmp = strtoll(optarg, NULL, 16);
        remotes[1] = (uint16_t)tmp;
        break;
      case '3':
        tmp = strtoll(optarg, NULL, 16);
        remotes[2] = (uint16_t)tmp;
        break;
      case '4':
        tmp = strtoll(optarg, NULL, 16);
        remotes[3] = (uint16_t)tmp;
        break;
      case 'c':
        tmp = strtoll(optarg, NULL, 16);
        color = (uint8_t)tmp;
        break;
      case 'b':
        tmp = strtoll(optarg, NULL, 16);
        bright = (uint8_t)tmp;
        break;
      case 'k':
        tmp = strtoll(optarg, NULL, 16);
        key = (uint8_t)tmp;
        break;
      case 'v':
        tmp = strtoll(optarg, NULL, 16);
        seq = (uint8_t)tmp;
        break;
      case 'w':
        do_command = 1;
        command = strtoll(optarg, NULL, 16);
        break;
      case '?':
        if(optopt == 'n' || optopt == 'p' ||
           optopt == 'c' || optopt == 'b' ||
           optopt == 'k' || optopt == 'w' ||
           optopt == '1' || optopt == '2' ||
           optopt == '3' || optopt == '4'){
          fprintf(stderr, "Option -%c requires an argument.\n", optopt);
        }
        else if(isprint(optopt)){
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        }
        else{
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        }
        return 1;
      default:
        fprintf(stderr, "Error parsing options");
        return -1;
    }
  }

  int ret = mlr.begin();

  if(ret < 0){
    fprintf(stderr, "Failed to open connection to the 2.4GHz module.\n");
    fprintf(stderr, "Make sure to run this program as root (sudo)\n\n");
    usage(argv[0], options);
    exit(-1);
  }

  if(do_receive){
    printf("Receiving mode, press Ctrl-C to end\n");
    receive();
  }

  if(do_udp){
    printf("UDP mode (raw), press Ctrl-C to end\n");
    udp_raw(udp_port);
  }

  if(do_milight){
    printf("UDP mode (milight), press Ctrl-C to end\n");
    uint16_t udp_ports[] = { 8891, 8892, 8893, 8894 };
    //uint16_t remotes[] = { 0x0001, 0xf746, 0x788e, 0x796a };
    udp_milight(udp_ports, remotes, resends, maxRemotes);
  }

  if(do_fade){
    printf("Fade mode, press Ctrl-C to end\n");
    fade(prefix, remotes[0], color, bright, resends);
  }

  if(do_strobe){
    printf("Strobe mode, press Ctrl-C to end\n");
    strobe(prefix, remotes[0], bright, resends);
  }

  /*
  double from = getTime();

  for(int i = 0; i < 200; i++){
    send(color, 0x00, 0x0F);
    color += 64;
  }

  double time = getTime() - from;
  printf("Time: %f %f %f\n", time, time / 200, 1 / (time/200));
  */

  if(do_command){
    send(command);
  }
  else{
    send(color, bright, key, remotes[0], prefix, seq, resends);
  }

  return 0;
}
