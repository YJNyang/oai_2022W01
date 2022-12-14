/*
* Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
* contributor license agreements.  See the NOTICE file distributed with
* this work for additional information regarding copyright ownership.
* The OpenAirInterface Software Alliance licenses this file to You under
* the OAI Public License, Version 1.1  (the "License"); you may not use this file
* except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.openairinterface.org/?page_id=698
*
* Author and copyright: Laurent Thomas, open-cells.com
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*-------------------------------------------------------------------------------
* For more information about the OpenAirInterface (OAI) Software Alliance:
*      contact@openairinterface.org
*/


/*
 * Open issues and limitations
 * The read and write should be called in the same thread, that is not new USRP UHD design
 * When the opposite side switch from passive reading to active R+Write, the synchro is not fully deterministic
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/epoll.h>
#include <string.h>

#include <common/utils/assertions.h>
#include <common/utils/LOG/log.h>
#include <common/utils/load_module_shlib.h>
#include <common/utils/telnetsrv/telnetsrv.h>
#include <common/config/config_userapi.h>
#include "common_lib.h"
#include <openair1/PHY/defs_eNB.h>
#include "openair1/PHY/defs_UE.h"
#define CHANNELMOD_DYNAMICLOAD
#include <openair1/SIMULATION/TOOLS/sim.h>
#include <targets/ARCH/rfsimulator/rfsimulator.h>
//#include "openair1/PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"

#define PORT 4043 //default TCP port for this simulator
#define CirSize 6144000 // 100ms is enough
#define sampleToByte(a,b) ((a)*(b)*sizeof(sample_t))
#define byteToSample(a,b) ((a)/(sizeof(sample_t)*(b)))

#define MAX_SIMULATION_CONNECTED_NODES 5
#define GENERATE_CHANNEL 10 //each frame in DL 


//

#define RFSIMU_SECTION    "rfsimulator"
#define RFSIMU_OPTIONS_PARAMNAME "options"


#define RFSIM_CONFIG_HELP_OPTIONS     " list of comma separated options to enable rf simulator functionalities. Available options: \n"\
  "        chanmod:   enable channel modelisation\n"\
  "        saviq:     enable saving written iqs to a file\n"
/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                            configuration parameters for the rfsimulator device                                                                              */
/*   optname                     helpstr                     paramflags           XXXptr                               defXXXval                          type         numelt  */
/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define simOpt PARAMFLAG_NOFREE|PARAMFLAG_CMDLINE_NOPREFIXENABLED
#define RFSIMULATOR_PARAMS_DESC {					\
    {"serveraddr",             "<ip address to connect to>\n",        simOpt,  strptr:&(rfsimulator->ip),              defstrval:"127.0.0.1",           TYPE_STRING,    0 },\
    {"serverport",             "<port to connect to>\n",              simOpt,  u16ptr:&(rfsimulator->port),            defuintval:PORT,                 TYPE_UINT16,    0 },\
    {RFSIMU_OPTIONS_PARAMNAME, RFSIM_CONFIG_HELP_OPTIONS,             0,       strlistptr:NULL,                        defstrlistval:NULL,              TYPE_STRINGLIST,0 },\
    {"IQfile",                 "<file path to use when saving IQs>\n",simOpt,  strptr:&(saveF),                        defstrval:"/tmp/rfsimulator.iqs",TYPE_STRING,    0 },\
    {"modelname",              "<channel model name>\n",              simOpt,  strptr:&(modelname),                    defstrval:"AWGN",                TYPE_STRING,    0 },\
    {"ploss",                  "<channel path loss in dB>\n",         simOpt,  dblptr:&(rfsimulator->chan_pathloss),   defdblval:0,                     TYPE_DOUBLE,    0 },\
    {"forgetfact",             "<channel forget factor ((0 to 1)>\n", simOpt,  dblptr:&(rfsimulator->chan_forgetfact), defdblval:0,                     TYPE_DOUBLE,    0 },\
    {"offset",                 "<channel offset in samps>\n",         simOpt,  iptr:&(rfsimulator->chan_offset),       defintval:0,                     TYPE_INT,       0 }\
  };
#define NUM_DELAY 10  //add_yjn

typedef struct complex16 sample_t; // 2*16 bits complex number

//add_yjn
static void fre_offset_compensation_simd(int32_t* rxdata, int start, int end, double off_angle) {
    int nb_samples_within_simd = 8;
    float phase_re = (float)cos(nb_samples_within_simd * off_angle);
    float phase_im = (float)sin(nb_samples_within_simd * off_angle);
    __m256 base_phase_re = _mm256_set1_ps(phase_re);
    __m256 base_phase_im = _mm256_set1_ps(phase_im);
    __m256 real_phase_re = _mm256_setr_ps(cos(start * off_angle), cos((start + 1) * off_angle), cos((start + 2) * off_angle), cos((start + 3) * off_angle),
        cos((start + 4) * off_angle), cos((start + 5) * off_angle), cos((start + 6) * off_angle), cos((start + 7) * off_angle));
    __m256 real_phase_im = _mm256_setr_ps(sin(start * off_angle), sin((start + 1) * off_angle), sin((start + 2) * off_angle), sin((start + 3) * off_angle),
        sin((start + 4) * off_angle), sin((start + 5) * off_angle), sin((start + 6) * off_angle), sin((start + 7) * off_angle));

    __m256 real_phase_re_tem;
    __m256 real_phase_im_tem;
    for (int n = start; n < end; n += 8) {

        __m256 rx_re = _mm256_setr_ps((float)((short*)rxdata)[2 * n], (float)(((short*)rxdata))[2 * n + 2],
                                              (float)(((short*)rxdata))[2 * n + 4], (float)(((short*)rxdata))[2 * n + 6],
                                              (float)(((short*)rxdata))[2 * n + 8], (float)(((short*)rxdata))[2 * n + 10],
                                              (float)(((short*)rxdata))[2 * n + 12], (float)(((short*)rxdata))[2 * n + 14]);
        __m256 rx_im = _mm256_setr_ps((float)(((short*)rxdata))[2 * n + 1], (float)(((short*)rxdata))[2 * n + 3],
                                              (float)(((short*)rxdata))[2 * n + 5], (float)(((short*)rxdata))[2 * n + 7],
                                              (float)(((short*)rxdata))[2 * n + 9], (float)(((short*)rxdata))[2 * n + 11],
                                              (float)(((short*)rxdata))[2 * n + 13], (float)(((short*)rxdata))[2 * n + 15]);

        __m256 data_re = _mm256_fmsub_ps(rx_re, real_phase_re, _mm256_mul_ps(rx_im, real_phase_im)); 
        __m256 data_im = _mm256_fmadd_ps(rx_im, real_phase_re, _mm256_mul_ps(rx_re, real_phase_im)); 

        __m256i data_re_int = _mm256_cvtps_epi32(data_re);
        __m256i data_im_int = _mm256_cvtps_epi32(data_im);
        __m256i data = _mm256_blend_epi16(data_re_int, _mm256_slli_epi32(data_im_int, 16), 0b10101010);
        _mm256_store_si256((__m256i*)(rxdata+start) + (n - start) / 8, data);

        real_phase_re_tem = real_phase_re;
        real_phase_im_tem = real_phase_im;
        real_phase_re = _mm256_fmsub_ps(real_phase_re_tem, base_phase_re, _mm256_mul_ps(real_phase_im_tem, base_phase_im));
        real_phase_im = _mm256_fmadd_ps(real_phase_im_tem, base_phase_re, _mm256_mul_ps(real_phase_re_tem, base_phase_im));

    }
}

static void cfo_compensation(int32_t* rxdata, int64_t start, int64_t end, double off_angle){
  double re,im;
  int BlockSize = end - start;
  int alignedstart = 8-(start%8);
  int alignedend = BlockSize-(end%8);
  
  for(int n=start; n<start+alignedstart; n++){
    re = ((double)(((short *)rxdata))[2*n]);
    im = ((double)(((short *)rxdata))[2*n+1]);
    ((short *)rxdata)[2*n] = (short)(round(re*cos(n*off_angle) - im*sin(n*off_angle)));
    ((short *)rxdata)[2*n+1] = (short)(round(re*sin(n*off_angle) + im*cos(n*off_angle)));
  }

  fre_offset_compensation_simd(rxdata,start+alignedstart,start+alignedend,off_angle);

  for(int n=start+alignedend; n<start+BlockSize; n++){
      re = ((double)(((short *)rxdata))[2*n]);
      im = ((double)(((short *)rxdata))[2*n+1]);
      ((short *)rxdata)[2*n] = (short)(round(re*cos(n*off_angle) - im*sin(n*off_angle)));
      ((short *)rxdata)[2*n+1] = (short)(round(re*sin(n*off_angle) + im*cos(n*off_angle)));
  } 
  return;
}

static int rfsimu_setchanmod_cmd(char *buff, int debug, telnet_printfunc_t prnt, void *arg);
static telnetshell_cmddef_t rfsimu_cmdarray[] = {
  {"setmodel","<model name> <model type>",(cmdfunc_t)rfsimu_setchanmod_cmd,TELNETSRV_CMDFLAG_PUSHINTPOOLQ},
  {"","",NULL},
};

static telnetshell_vardef_t rfsimu_vardef[] = {
  {"",0,NULL}
};
pthread_mutex_t Sockmutex;


typedef struct buffer_s {
  int conn_sock;
  openair0_timestamp lastReceivedTS;
  bool headerMode;
  bool trashingPacket;
  samplesBlockHeader_t th;
  char *transferPtr;
  uint64_t remainToTransfer;
  char *circularBufEnd;
  sample_t *circularBuf;
  channel_desc_t *channel_model;
} buffer_t;

typedef struct {
  int listen_sock, epollfd;
  openair0_timestamp nextTimestamp;
  openair0_timestamp lastWroteTS;
  uint64_t typeStamp;
  char *ip;
  uint16_t port;
  int saveIQfile;
  buffer_t buf[FD_SETSIZE];
  int rx_num_channels;
  int tx_num_channels;
  double sample_rate;
  double tx_bw;
  int channelmod;
  double chan_pathloss;
  double chan_forgetfact;
  int    chan_offset;
  float  noise_power_dB;
  void *telnetcmd_qid;
  poll_telnetcmdq_func_t poll_telnetcmdq;
} rfsimulator_state_t;


static void allocCirBuf(rfsimulator_state_t *bridge, int sock) {
  buffer_t *ptr=&bridge->buf[sock];
  AssertFatal ( (ptr->circularBuf=(sample_t *) memalign(32, sampleToByte(CirSize,1))) != NULL, "");
  ptr->circularBufEnd=((char *)ptr->circularBuf)+sampleToByte(CirSize,1);
  ptr->conn_sock=sock;
  ptr->lastReceivedTS=0;
  ptr->headerMode=true;
  ptr->trashingPacket=false;
  ptr->transferPtr=(char *)&ptr->th;
  ptr->remainToTransfer=sizeof(samplesBlockHeader_t);
  int sendbuff=1000*1000*100;
  AssertFatal ( setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff)) == 0, "");
  struct epoll_event ev= {0};
  ev.events = EPOLLIN | EPOLLRDHUP;
  ev.data.fd = sock;
  AssertFatal(epoll_ctl(bridge->epollfd, EPOLL_CTL_ADD,  sock, &ev) != -1, "");
  if ( bridge->channelmod > 0) {
    // create channel simulation model for this mode reception
    // snr_dB is pure global, coming from configuration paramter "-s"
    // Fixme: referenceSignalPower should come from the right place
    // but the datamodel is inconsistant
    // legacy: RC.ru[ru_id]->frame_parms.pdsch_config_common.referenceSignalPower
    // (must not come from ru[]->frame_parms as it doesn't belong to ru !!!)
    // Legacy sets it as:
    // ptr->channel_model->path_loss_dB = -132.24 + snr_dB - RC.ru[0]->frame_parms->pdsch_config_common.referenceSignalPower;
    // we use directly the paramter passed on the command line ("-s")
    // the value channel_model->path_loss_dB seems only a storage place (new_channel_desc_scm() only copy the passed value)
    // Legacy changes directlty the variable channel_model->path_loss_dB place to place
    // while calling new_channel_desc_scm() with path losses = 0
    static bool init_done=false;

    if (!init_done) {
      uint64_t rand;
      FILE *h=fopen("/dev/random","r");

      if ( 1 != fread(&rand,sizeof(rand),1,h) )
        LOG_W(HW, "Simulator can't read /dev/random\n");

      fclose(h);
      randominit(rand);
      tableNor(rand);
      init_done=true;
    }
    char *modelname = (bridge->typeStamp == ENB_MAGICDL) ? "rfsimu_channel_ue0":"rfsimu_channel_enB0";
    ptr->channel_model=find_channel_desc_fromname(modelname); // path_loss in dB
    AssertFatal((ptr->channel_model!= NULL),"Channel model %s not found, check config file\n",modelname);
    set_channeldesc_owner(ptr->channel_model, RFSIMU_MODULEID);
    random_channel(ptr->channel_model,false);
  }
}

static void removeCirBuf(rfsimulator_state_t *bridge, int sock) {
  AssertFatal( epoll_ctl(bridge->epollfd, EPOLL_CTL_DEL,  sock, NULL) != -1, "");
  close(sock);
  free(bridge->buf[sock].circularBuf);
  // Fixme: no free_channel_desc_scm(bridge->buf[sock].channel_model) implemented
  // a lot of mem leaks
  free(bridge->buf[sock].channel_model);
  memset(&bridge->buf[sock], 0, sizeof(buffer_t));
  bridge->buf[sock].conn_sock=-1;
}

static void socketError(rfsimulator_state_t *bridge, int sock) {
  if (bridge->buf[sock].conn_sock!=-1) {
    LOG_W(HW,"Lost socket \n");
    removeCirBuf(bridge, sock);

    if (bridge->typeStamp==UE_MAGICDL)
      exit(1);
  }
}

#define helpTxt "\
\x1b[31m\
rfsimulator: error: you have to run one UE and one eNB\n\
For this, export RFSIMULATOR=enb (eNB case) or \n\
                 RFSIMULATOR=<an ip address> (UE case)\n\
                 or use rfsimulator.serveraddr configuration option\n\
\x1b[m"

enum  blocking_t {
  notBlocking,
  blocking
};

static void setblocking(int sock, enum blocking_t active) {
  int opts;
  AssertFatal( (opts = fcntl(sock, F_GETFL)) >= 0,"");

  if (active==blocking)
    opts = opts & ~O_NONBLOCK;
  else
    opts = opts | O_NONBLOCK;

  AssertFatal(fcntl(sock, F_SETFL, opts) >= 0, "");
}

static bool flushInput(rfsimulator_state_t *t, int timeout, int nsamps);

static void fullwrite(int fd, void *_buf, ssize_t count, rfsimulator_state_t *t) {
  if (t->saveIQfile != -1) {
    if (write(t->saveIQfile, _buf, count) != count )
      LOG_E(HW,"write in save iq file failed (%s)\n",strerror(errno));
  }

  AssertFatal(fd>=0 && _buf && count >0 && t,
              "Bug: %d/%p/%zd/%p", fd, _buf, count, t);
  char *buf = _buf;
  ssize_t l;
  setblocking(fd, notBlocking);

  while (count) {
    l = write(fd, buf, count);

    if (l <= 0) {
      if (errno==EINTR)
        continue;

      if(errno==EAGAIN) {
        // The opposite side is saturated
        // we read incoming sockets meawhile waiting
        //flushInput(t, 5);
        usleep(500);
        continue;
      } else
        return;
    }

    count -= l;
    buf += l;
  }
}

static void rfsimulator_readconfig(rfsimulator_state_t *rfsimulator) {
  char *saveF=NULL;
  char *modelname=NULL;
  paramdef_t rfsimu_params[] = RFSIMULATOR_PARAMS_DESC;
  int p = config_paramidx_fromname(rfsimu_params,sizeof(rfsimu_params)/sizeof(paramdef_t), RFSIMU_OPTIONS_PARAMNAME) ;
  int ret = config_get( rfsimu_params,sizeof(rfsimu_params)/sizeof(paramdef_t),RFSIMU_SECTION);
  AssertFatal(ret >= 0, "configuration couldn't be performed");
  rfsimulator->saveIQfile = -1;

  for(int i=0; i<rfsimu_params[p].numelt ; i++) {
    if (strcmp(rfsimu_params[p].strlistptr[i],"saviq") == 0) {
      rfsimulator->saveIQfile=open(saveF,O_APPEND| O_CREAT|O_TRUNC | O_WRONLY, 0666);

      if ( rfsimulator->saveIQfile != -1 )
        LOG_I(HW,"rfsimulator: will save written IQ samples  in %s\n", saveF);
      else
        LOG_E(HW, "can't open %s for IQ saving (%s)\n", saveF, strerror(errno));

      break;
    } else if (strcmp(rfsimu_params[p].strlistptr[i],"chanmod") == 0) {
      init_channelmod();
      load_channellist(rfsimulator->tx_num_channels, rfsimulator->rx_num_channels, rfsimulator->sample_rate, rfsimulator->tx_bw);
      rfsimulator->channelmod=true;
    } else {
      fprintf(stderr,"Unknown rfsimulator option: %s\n",rfsimu_params[p].strlistptr[i]);
      exit(-1);
    }
  }

  /* for compatibility keep environment variable usage */
  if ( getenv("RFSIMULATOR") != NULL ) {
    rfsimulator->ip=getenv("RFSIMULATOR");
  }

  if ( strncasecmp(rfsimulator->ip,"enb",3) == 0 ||
       strncasecmp(rfsimulator->ip,"server",3) == 0 )
    rfsimulator->typeStamp = ENB_MAGICDL;
  else
    rfsimulator->typeStamp = UE_MAGICDL;
}

static int rfsimu_setchanmod_cmd(char *buff, int debug, telnet_printfunc_t prnt, void *arg) {
  char *modelname=NULL;
  char *modeltype=NULL;
  if (debug)
  	  prnt("rfsimu_setchanmod_cmd buffer \"%s\"\n",buff);
  int s = sscanf(buff,"%m[^ ] %ms\n",&modelname, &modeltype);

  if (s == 2) {
    int channelmod=modelid_fromstrtype(modeltype);

    if (channelmod<0)
      prnt("ERROR: model type %s unknown\n",modeltype);
    else {
      rfsimulator_state_t *t = (rfsimulator_state_t *)arg;
      int found=0;
      for (int i=0; i<FD_SETSIZE; i++) {
        buffer_t *b=&t->buf[i];
        if ( b->channel_model==NULL)
          continue;
        if (b->channel_model->model_name==NULL)
          continue;
        if (b->conn_sock >= 0 && (strcmp(b->channel_model->model_name,modelname)==0)) {

          channel_desc_t *newmodel=new_channel_desc_scm(t->tx_num_channels,t->rx_num_channels,
                                   channelmod,
                                   t->sample_rate,
                                   t->tx_bw,
                                   30e-9,  // TDL delay-spread parameter
                                   t->chan_forgetfact, // forgetting_factor
                                   t->chan_offset, // maybe used for TA
							       t->chan_pathloss,
							       t->noise_power_dB
							       ); // path_loss in dB
          set_channeldesc_owner(newmodel, RFSIMU_MODULEID);
          set_channeldesc_name(newmodel,modelname);
          random_channel(newmodel,false);
          channel_desc_t *oldmodel=b->channel_model;
          b->channel_model=newmodel;
          free_channel_desc_scm(oldmodel);
          prnt("New model type %s applied to channel %s connected to sock %d\n",modeltype,modelname,i);
          found=1;
          break;
        }
      } /* for */
      if (found==0)
      	prnt("Channel %s not found or not currently used\n",modelname); 
    }
  } else {
    prnt("ERROR: 2 parameters required: model name and model type (%i found)\n",s);
  }

  free(modelname);
  free(modeltype);
  return CMDSTATUS_FOUND;
}

static int startServer(openair0_device *device) {
  rfsimulator_state_t *t = (rfsimulator_state_t *) device->priv;
  t->typeStamp=ENB_MAGICDL;
  AssertFatal((t->listen_sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0, "");
  int enable = 1;
  AssertFatal(setsockopt(t->listen_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == 0, "");
  struct sockaddr_in addr = {
sin_family:
    AF_INET,
sin_port:
    htons(t->port),
sin_addr:
    { s_addr: INADDR_ANY }
  };
  bind(t->listen_sock, (struct sockaddr *)&addr, sizeof(addr));
  AssertFatal(listen(t->listen_sock, 5) == 0, "");
  struct epoll_event ev= {0};
  ev.events = EPOLLIN;
  ev.data.fd = t->listen_sock;
  AssertFatal(epoll_ctl(t->epollfd, EPOLL_CTL_ADD,  t->listen_sock, &ev) != -1, "");
  return 0;
}

static int startClient(openair0_device *device) {
  rfsimulator_state_t *t = device->priv;
  t->typeStamp=UE_MAGICDL;
  int sock;
  AssertFatal((sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0, "");
  struct sockaddr_in addr = {
sin_family:
    AF_INET,
sin_port:
    htons(t->port),
sin_addr:
    { s_addr: INADDR_ANY }
  };
  addr.sin_addr.s_addr = inet_addr(t->ip);
  bool connected=false;

  while(!connected) {
    LOG_I(HW,"rfsimulator: trying to connect to %s:%d\n", t->ip, t->port);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
      LOG_I(HW,"rfsimulator: connection established\n");
      connected=true;
    }

    perror("rfsimulator");
    sleep(1);
  }

  setblocking(sock, notBlocking);
  allocCirBuf(t, sock);
  return 0;
}

static int rfsimulator_write_internal(rfsimulator_state_t *t, openair0_timestamp timestamp, void **samplesVoid, int nsamps, int nbAnt, int flags, bool alreadyLocked) {
  if (!alreadyLocked)
    pthread_mutex_lock(&Sockmutex);

  LOG_D(HW,"sending %d samples at time: %ld\n", nsamps, timestamp);

  for (int i=0; i<FD_SETSIZE; i++) {
    buffer_t *b=&t->buf[i];

    if (b->conn_sock >= 0 ) {
      samplesBlockHeader_t header= {t->typeStamp, nsamps, nbAnt, timestamp};
      fullwrite(b->conn_sock,&header, sizeof(header), t);
      sample_t tmpSamples[nsamps][nbAnt];

      for(int a=0; a<nbAnt; a++) {
        sample_t *in=(sample_t *)samplesVoid[a];

        for(int s=0; s<nsamps; s++)
          tmpSamples[s][a]=in[s];
      }

      if (b->conn_sock >= 0 ) {
        fullwrite(b->conn_sock, (void *)tmpSamples, sampleToByte(nsamps,nbAnt), t);
      }
    }
  }

  if ( t->lastWroteTS != 0 && abs((double)t->lastWroteTS-timestamp) > (double)CirSize)
    LOG_E(HW,"Discontinuous TX gap too large Tx:%lu, %lu\n", t->lastWroteTS, timestamp);

  if (t->lastWroteTS > timestamp+nsamps)
    LOG_E(HW,"Not supported to send Tx out of order (same in USRP) %lu, %lu\n",
          t->lastWroteTS, timestamp);

  t->lastWroteTS=timestamp+nsamps;

  if (!alreadyLocked)
    pthread_mutex_unlock(&Sockmutex);

  LOG_D(HW,"sent %d samples at time: %ld->%ld, energy in first antenna: %d\n",
        nsamps, timestamp, timestamp+nsamps, signal_energy(samplesVoid[0], nsamps) );
  return nsamps;
}

static int rfsimulator_write(openair0_device *device, openair0_timestamp timestamp, void **samplesVoid, int nsamps, int nbAnt, int flags) {
  return rfsimulator_write_internal(device->priv, timestamp, samplesVoid, nsamps, nbAnt, flags, false);
}

static bool flushInput(rfsimulator_state_t *t, int timeout, int nsamps_for_initial) {
  // Process all incoming events on sockets
  // store the data in lists
  struct epoll_event events[FD_SETSIZE]= {{0}};
  int nfds = epoll_wait(t->epollfd, events, FD_SETSIZE, timeout);

  if ( nfds==-1 ) {
    if ( errno==EINTR || errno==EAGAIN ) {
      return false;
    } else
      AssertFatal(false,"error in epoll_wait\n");
  }

  for (int nbEv = 0; nbEv < nfds; ++nbEv) {
    int fd=events[nbEv].data.fd;

    if (events[nbEv].events & EPOLLIN && fd == t->listen_sock) {
      int conn_sock;
      AssertFatal( (conn_sock = accept(t->listen_sock,NULL,NULL)) != -1, "");
      setblocking(conn_sock, notBlocking);
      allocCirBuf(t, conn_sock);
      LOG_I(HW,"A client connected, sending the current time\n");
      struct complex16 v= {0};
      void *samplesVoid[t->tx_num_channels];

      for ( int i=0; i < t->tx_num_channels; i++)
        samplesVoid[i]=(void *)&v;

      rfsimulator_write_internal(t, t->lastWroteTS > 1 ? t->lastWroteTS-1 : 0,
                                 samplesVoid, 1,
                                 t->tx_num_channels, 1, false);
    } else {
      if ( events[nbEv].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP) ) {
        socketError(t,fd);
        continue;
      }

      buffer_t *b=&t->buf[fd];

      if ( b->circularBuf == NULL ) {
        LOG_E(HW, "received data on not connected socket %d\n", events[nbEv].data.fd);
        continue;
      }

      ssize_t blockSz;

      if ( b->headerMode)
        blockSz=b->remainToTransfer;
      else
        blockSz= b->transferPtr + b->remainToTransfer <= b->circularBufEnd ?
                 b->remainToTransfer :
                 b->circularBufEnd - b->transferPtr ;

      ssize_t sz=recv(fd, b->transferPtr, blockSz, MSG_DONTWAIT);

      if ( sz < 0 ) {
        if ( errno != EAGAIN ) {
          LOG_E(HW,"socket failed %s\n", strerror(errno));
          //abort();
        }
      } else if ( sz == 0 )
        continue;

      LOG_D(HW, "Socket rcv %zd bytes\n", sz);
      AssertFatal((b->remainToTransfer-=sz) >= 0, "");
      b->transferPtr+=sz;

      if (b->transferPtr==b->circularBufEnd )
        b->transferPtr=(char *)b->circularBuf;

      // check the header and start block transfer
      if ( b->headerMode==true && b->remainToTransfer==0) {
        AssertFatal( (t->typeStamp == UE_MAGICDL  && b->th.magic==ENB_MAGICDL) ||
                     (t->typeStamp == ENB_MAGICDL && b->th.magic==UE_MAGICDL), "Socket Error in protocol");
        b->headerMode=false;

        if ( t->nextTimestamp == 0 ) { // First block in UE, resync with the eNB current TS
          t->nextTimestamp=b->th.timestamp> nsamps_for_initial ?
                           b->th.timestamp -  nsamps_for_initial :
                           0;
          b->lastReceivedTS=b->th.timestamp> nsamps_for_initial ?
                            b->th.timestamp :
                            nsamps_for_initial;
          LOG_W(HW,"UE got first timestamp: starting at %lu\n",  t->nextTimestamp);
          b->trashingPacket=true;
        } else if ( b->lastReceivedTS < b->th.timestamp) {
          int nbAnt= b->th.nbAnt;

          if ( b->th.timestamp-b->lastReceivedTS < CirSize ) {
            for (uint64_t index=b->lastReceivedTS; index < b->th.timestamp; index++ ) {
              for (int a=0; a < nbAnt; a++) {
                b->circularBuf[(index*nbAnt+a)%CirSize].r = 0;
                b->circularBuf[(index*nbAnt+a)%CirSize].i = 0;
              }
            }
          } else {
            memset(b->circularBuf, 0, sampleToByte(CirSize,1));
          }

          if (b->lastReceivedTS != 0 && b->th.timestamp-b->lastReceivedTS < 1000)
            LOG_W(HW,"UEsock: %d gap of: %ld in reception\n", fd, b->th.timestamp-b->lastReceivedTS );

          b->lastReceivedTS=b->th.timestamp;
        } else if ( b->lastReceivedTS > b->th.timestamp && b->th.size == 1 ) {
          LOG_W(HW,"Received Rx/Tx synchro out of order\n");
          b->trashingPacket=true;
        } else if ( b->lastReceivedTS == b->th.timestamp ) {
          // normal case
        } else {
          LOG_E(HW, "received data in past: current is %lu, new reception: %lu!\n", b->lastReceivedTS, b->th.timestamp);
          b->trashingPacket=true;
        }

        pthread_mutex_lock(&Sockmutex);

        if (t->lastWroteTS != 0 && ( abs((double)t->lastWroteTS-b->lastReceivedTS) > (double)CirSize))
          LOG_E(HW,"UEsock: %d Tx/Rx shift too large Tx:%lu, Rx:%lu\n", fd, t->lastWroteTS, b->lastReceivedTS);

        pthread_mutex_unlock(&Sockmutex);
        b->transferPtr=(char *)&b->circularBuf[((b->lastReceivedTS+NUM_DELAY*30720)*b->th.nbAnt)%CirSize]; //add_yjn
        b->remainToTransfer=sampleToByte(b->th.size, b->th.nbAnt);
      }

      if ( b->headerMode==false ) {
        if ( ! b->trashingPacket ) {
          b->lastReceivedTS=b->th.timestamp+b->th.size-byteToSample(b->remainToTransfer,b->th.nbAnt);
          LOG_D(HW,"UEsock: %d Set b->lastReceivedTS %ld\n", fd, b->lastReceivedTS);
        }

        if ( b->remainToTransfer==0) {
          LOG_D(HW,"UEsock: %d Completed block reception: %ld\n", fd, b->lastReceivedTS);
          b->headerMode=true;
          b->transferPtr=(char *)&b->th;
          b->remainToTransfer=sizeof(samplesBlockHeader_t);
          b->th.magic=-1;
          b->trashingPacket=false;
        }
      }
    }
  }

  return nfds>0;
}

static int rfsimulator_read(openair0_device *device, openair0_timestamp *ptimestamp, void **samplesVoid, int nsamps, int nbAnt) {
  if (nbAnt > 4) {
    LOG_W(HW, "rfsimulator: only 4 antenna tested\n");
  }

  rfsimulator_state_t *t = device->priv;
  LOG_D(HW, "Enter rfsimulator_read, expect %d samples, will release at TS: %ld\n", nsamps, t->nextTimestamp+nsamps);
  // deliver data from received data
  // check if a UE is connected
  int first_sock;

  for (first_sock=0; first_sock<FD_SETSIZE; first_sock++)
    if (t->buf[first_sock].circularBuf != NULL )
      break;

  if ( first_sock ==  FD_SETSIZE ) {
    // no connected device (we are eNB, no UE is connected)
    if ( t->nextTimestamp == 0)
      LOG_W(HW,"No connected device, generating void samples...\n");

    if (!flushInput(t, 10,  nsamps)) {
      for (int x=0; x < nbAnt; x++)
        memset(samplesVoid[x],0,sampleToByte(nsamps,1));

      t->nextTimestamp+=nsamps;

      if ( ((t->nextTimestamp/nsamps)%100) == 0)
        LOG_D(HW,"No UE, Generated void samples for Rx: %ld\n", t->nextTimestamp);

      *ptimestamp = t->nextTimestamp-nsamps;
      return nsamps;
    }
  } else {
    pthread_mutex_lock(&Sockmutex);

    if ( t->nextTimestamp > 0 && t->lastWroteTS < t->nextTimestamp) {
      pthread_mutex_unlock(&Sockmutex);
      usleep(10000);
      pthread_mutex_lock(&Sockmutex);

      if ( t->lastWroteTS < t->nextTimestamp ) {
        // Assuming Tx is not done fully in another thread
        // We can never write is the past from the received time
        // So, the node perform receive but will never write these symbols
        // let's tell this to the opposite node
        // We send timestamp for nb samples required
        // assuming this should have been done earlier if a Tx would exist
        pthread_mutex_unlock(&Sockmutex);
        struct complex16 v= {0};
        void *dummyS[t->tx_num_channels];

        for ( int i=0; i < t->tx_num_channels; i++)
          dummyS[i]=(void *)&v;

        LOG_I(HW, "No samples Tx occured, so we send 1 sample to notify it: Tx:%lu, Rx:%lu\n",
              t->lastWroteTS, t->nextTimestamp);
        rfsimulator_write_internal(t, t->nextTimestamp,
                                   dummyS, 1,
                                   t->tx_num_channels, 1, true);
      } else {
        pthread_mutex_unlock(&Sockmutex);
        LOG_W(HW, "trx_write came from another thread\n");
      }
    } else
      pthread_mutex_unlock(&Sockmutex);

    bool have_to_wait;

    do {
      have_to_wait=false;

      for ( int sock=0; sock<FD_SETSIZE; sock++) {
        buffer_t *b=&t->buf[sock];

        if ( b->circularBuf )
          if ( t->nextTimestamp+nsamps > b->lastReceivedTS ) {
            have_to_wait=true;
            break;
          }
      }

      if (have_to_wait)
        /*printf("Waiting on socket, current last ts: %ld, expected at least : %ld\n",
          ptr->lastReceivedTS,
          t->nextTimestamp+nsamps);
        */
        flushInput(t, 3, nsamps);
    } while (have_to_wait);
  }

  // Clear the output buffer
  for (int a=0; a<nbAnt; a++)
    memset(samplesVoid[a],0,sampleToByte(nsamps,1));

  // Add all input nodes signal in the output buffer
  for (int sock=0; sock<FD_SETSIZE; sock++) {
    buffer_t *ptr=&t->buf[sock];

    if ( ptr->circularBuf ) {
      bool reGenerateChannel=false;

      //fixme: when do we regenerate
      // it seems legacy behavior is: never in UL, each frame in DL
      if (reGenerateChannel)
        random_channel(ptr->channel_model,0);

      if (t->poll_telnetcmdq)
        t->poll_telnetcmdq(t->telnetcmd_qid,t);

      for (int a=0; a<nbAnt; a++) {//loop over number of Rx antennas
        // add_yjn
        int samples_per_subframe = 2 * 30720;
        float s_time = 1 / (1.0e3 * samples_per_subframe);      // sampling time
        static float freq_offset_dynamic = 0;                   //???????????? (-60k, 60k)
        static float freq_offset_static = -45320;                    //???????????? 
        static int flag = 0;                                    //1 The initial dynamic frequency offset increases; -1 the initial dynamic frequency offset decreases; 0 without dynamic frequency offset
        int freq_offset_samples_step = samples_per_subframe/2;  //The sampling number of dynamic frequency offset changes; 0.35Hz per time slot, and the current change is in the unit of time slot.
        float freq_offset_change_step = 0.35;                   
        int32_t* tx_data =  (int32_t *)(&(ptr->circularBuf[0]));  //CirSize is the number of samples to the circularBuf
        int64_t fre_offset_timestamp_start = 0;
        int64_t fre_offset_timestamp_end = 0;
        int i = 0;


 
        // for (int i=0; i<16;i++)
        // {
        //   printf("==============data[i]=%d\n",tx_data[(t->nextTimestamp)%CirSize+i]);
        // }
        for (i=0; i<(nsamps/freq_offset_samples_step); i++){
          /*Determine whether the sampling point exceeds the buffer boundary*/
          fre_offset_timestamp_start = (((t->nextTimestamp)%CirSize + i * freq_offset_samples_step) < CirSize)?  ((t->nextTimestamp)%CirSize + i * freq_offset_samples_step) : ((t->nextTimestamp)%CirSize + i * freq_offset_samples_step)%CirSize;

          if (((t->nextTimestamp)%CirSize + i * freq_offset_samples_step) < CirSize) //???????????????????????????????????????????????????
            fre_offset_timestamp_end = (((t->nextTimestamp)%CirSize + (i+1) * freq_offset_samples_step) < CirSize)? ((t->nextTimestamp)%CirSize + (i+1) * freq_offset_samples_step) : CirSize;
          else if (((t->nextTimestamp)%CirSize + i * freq_offset_samples_step) >= CirSize) 
            fre_offset_timestamp_end = ((t->nextTimestamp)%CirSize + (i+1) * freq_offset_samples_step)%CirSize;
          
          /*Set dynamic frequency offset growth according to flag*/
          if (flag == 1){
            freq_offset_dynamic = freq_offset_dynamic + freq_offset_change_step;
            if(freq_offset_dynamic >= 59999.7) flag = -1;
          }else if (flag == -1){
            freq_offset_dynamic = freq_offset_dynamic - freq_offset_change_step;
            if(freq_offset_dynamic <= -59999.7) flag = 1;
          }else {
            freq_offset_dynamic = 0;
          }

          /*  */
          //LOG_W(HW, "=============================================================================================\n");
          float freq_offset = freq_offset_static + freq_offset_dynamic;
          double off_angle = 2 * M_PI * s_time * (freq_offset);  // offset rotation angle compensation per sample
          if(fre_offset_timestamp_end == CirSize){  //???????????????????????????????????????????????????
            cfo_compensation(tx_data, fre_offset_timestamp_start, CirSize, off_angle);
            cfo_compensation(tx_data, 0, ((t->nextTimestamp)%CirSize + (i+1) * freq_offset_samples_step)%CirSize, off_angle);
            //LOG_W(HW, "cfo_compensation start is %ld,end is %ld\n",fre_offset_timestamp_start,((t->nextTimestamp)%CirSize + (i+1) * freq_offset_samples_step)%CirSize);
          }else{
            cfo_compensation(tx_data, fre_offset_timestamp_start, fre_offset_timestamp_end, off_angle);//??????????????????????????????
           // LOG_W(HW, "cfo_compensation start is %ld,end is %ld\n",fre_offset_timestamp_start,fre_offset_timestamp_end);
          } 
        }
        

        /* The number of sampling points less than one time slot is the frequency offset of the previous time slot */
        if(nsamps-i*freq_offset_samples_step){
          fre_offset_timestamp_start = (((t->nextTimestamp)%CirSize + i * freq_offset_samples_step) < CirSize)?  ((t->nextTimestamp)%CirSize + i * freq_offset_samples_step) : ((t->nextTimestamp)%CirSize + i * freq_offset_samples_step)%CirSize;

          if (((t->nextTimestamp)%CirSize + i * freq_offset_samples_step) < CirSize) 
            fre_offset_timestamp_end = (((t->nextTimestamp)%CirSize + nsamps) < CirSize)? ((t->nextTimestamp)%CirSize + nsamps) : CirSize;
          else if (((t->nextTimestamp)%CirSize + i * freq_offset_samples_step) >= CirSize) 
            fre_offset_timestamp_end = ((t->nextTimestamp)%CirSize + nsamps)%CirSize;

          if(nsamps-i*freq_offset_samples_step > 20700)
          {
            if (flag == 1) freq_offset_dynamic += 0.35;
            else if (flag == -1) freq_offset_dynamic -= 0.35;
              static int32_t count_unint = 0;
              // printf("count_unint = %d\n",count_unint);
              count_unint++;
          }

          float freq_offset = freq_offset_static + freq_offset_dynamic;
          double off_angle = 2 * M_PI * s_time * (freq_offset);  // offset rotation angle compensation per sample
          if(fre_offset_timestamp_end == CirSize){
            cfo_compensation(tx_data, fre_offset_timestamp_start, CirSize, off_angle);
            cfo_compensation(tx_data, 0, ((t->nextTimestamp)%CirSize + nsamps)%CirSize, off_angle);
            //LOG_W(HW, "cfo_compensation start is %ld,end is %ld\n",fre_offset_timestamp_start,((t->nextTimestamp)%CirSize + nsamps)%CirSize);
          }else{
            cfo_compensation(tx_data, fre_offset_timestamp_start, fre_offset_timestamp_end, off_angle);//??????????????????????????????
           // LOG_W(HW, "cfo_compensation start is %ld,end is %ld\n",fre_offset_timestamp_start,fre_offset_timestamp_end);
          } 
        }
        // LOG_W(HW, "=============================================================================================\n");
        // for (int i=0; i<16;i++)
        // {
        //   printf("==============data[i]=%d\n",tx_data[(t->nextTimestamp)%CirSize+i]);
        // }

        // printf("===============================freq_offset_static = %f\n",freq_offset_static);
        // printf("===============================freq_offset_dynamic = %f\n",freq_offset_dynamic);

        
        if ( ptr->channel_model != NULL ) // apply a channel model
        {
          rxAddInput( ptr->circularBuf, (struct complex16 *) samplesVoid[a],
                      a,
                      ptr->channel_model,
                      nsamps,
                      t->nextTimestamp,
                      CirSize
                    );
          //printf("===========================with channel modle nbAnt_tx = %d\n",ptr->channel_model->nb_tx);
        }
          
        else { // no channel modeling
          double H_awgn_mimo[4][4] ={{1.0, 0.5, 0.25, 0.125},//rx 0
                                     {0.5, 1.0, 0.5, 0.25},  //rx 1
                                     {0.25, 0.5, 1.0, 0.5},  //rx 2
                                     {0.125, 0.25, 0.5, 1.0}};//rx 3

          sample_t *out=(sample_t *)samplesVoid[a];
          int nbAnt_tx = ptr->th.nbAnt;//number of Tx antennas
          // LOG_W(HW, "Read Ant%d, ",a);
          // LOG_W(HW, "nbAnt_tx%d\n",nbAnt_tx);
          // LOG_W(HW, "circularBuf start is %ld,end is %ld\n",((t->nextTimestamp)*nbAnt_tx+nbAnt_tx-1)%CirSize,((t->nextTimestamp+nsamps)*nbAnt_tx+nbAnt_tx-1)%CirSize);
          // LOG_W(HW, "=============================================================================================\n");//LOG_I(HW, "no channel modle nbAnt_tx %d\n",nbAnt_tx);
          for (int i=0; i < nsamps; i++) {//loop over nsamps
            for (int a_tx=0; a_tx<nbAnt_tx; a_tx++) { //sum up signals from nbAnt_tx antennas
              //printf("====================================no channel modle nbAnt_tx = %d\n",nbAnt_tx);
              out[i].r += (short)(ptr->circularBuf[((t->nextTimestamp+i)*nbAnt_tx+a_tx)%CirSize].r*H_awgn_mimo[a][a_tx]);
              out[i].i += (short)(ptr->circularBuf[((t->nextTimestamp+i)*nbAnt_tx+a_tx)%CirSize].i*H_awgn_mimo[a][a_tx]);
            } // end for a_tx
          } // end for i (number of samps)
        } // end of no channel modeling
      } // end for a (number of rx antennas)
    }
  }

  *ptimestamp = t->nextTimestamp; // return the time of the first sample
  t->nextTimestamp+=nsamps;  //??????nextTimestamp?????????????????????
  LOG_D(HW,"Rx to upper layer: %d from %ld to %ld, energy in first antenna %d\n",
        nsamps,
        *ptimestamp, t->nextTimestamp,
        signal_energy(samplesVoid[0], nsamps));
  return nsamps;
}

static int rfsimulator_get_stats(openair0_device *device) {
  return 0;
}
static int rfsimulator_reset_stats(openair0_device *device) {
  return 0;
}
static void rfsimulator_end(openair0_device *device) {}
static int rfsimulator_stop(openair0_device *device) {
  return 0;
}
static int rfsimulator_set_freq(openair0_device *device, openair0_config_t *openair0_cfg,int exmimo_dump_config) {
  return 0;
}
static int rfsimulator_set_gains(openair0_device *device, openair0_config_t *openair0_cfg) {
  return 0;
}
static int rfsimulator_write_init(openair0_device *device) {
  return 0;
}
__attribute__((__visibility__("default")))
int device_init(openair0_device *device, openair0_config_t *openair0_cfg) {
  // to change the log level, use this on command line
  // --log_config.hw_log_level debug
  rfsimulator_state_t *rfsimulator = (rfsimulator_state_t *)calloc(sizeof(rfsimulator_state_t),1);
  // initialize channel simulation
  rfsimulator->tx_num_channels=openair0_cfg->tx_num_channels;
  rfsimulator->rx_num_channels=openair0_cfg->rx_num_channels;
  rfsimulator->sample_rate=openair0_cfg->sample_rate;
  rfsimulator->tx_bw=openair0_cfg->tx_bw;  
  rfsimulator_readconfig(rfsimulator);
  pthread_mutex_init(&Sockmutex, NULL);
  LOG_I(HW,"rfsimulator: running as %s\n", rfsimulator-> typeStamp == ENB_MAGICDL ? "server waiting opposite rfsimulators to connect" : "client: will connect to a rfsimulator server side");
  device->trx_start_func       = rfsimulator->typeStamp == ENB_MAGICDL ?
                                 startServer :
                                 startClient;
  device->trx_get_stats_func   = rfsimulator_get_stats;
  device->trx_reset_stats_func = rfsimulator_reset_stats;
  device->trx_end_func         = rfsimulator_end;
  device->trx_stop_func        = rfsimulator_stop;
  device->trx_set_freq_func    = rfsimulator_set_freq;
  device->trx_set_gains_func   = rfsimulator_set_gains;
  device->trx_write_func       = rfsimulator_write;
  device->trx_read_func      = rfsimulator_read;
  /* let's pretend to be a b2x0 */
  device->type = RFSIMULATOR;
  openair0_cfg[0].rx_gain[0] = 0;
  device->openair0_cfg=&openair0_cfg[0];
  device->priv = rfsimulator;
  device->trx_write_init = rfsimulator_write_init;

  for (int i=0; i<FD_SETSIZE; i++)
    rfsimulator->buf[i].conn_sock=-1;

  AssertFatal((rfsimulator->epollfd = epoll_create1(0)) != -1,"");

  //randominit(0);
  set_taus_seed(0);
  /* look for telnet server, if it is loaded, add the channel modeling commands to it */
  add_telnetcmd_func_t addcmd = (add_telnetcmd_func_t)get_shlibmodule_fptr("telnetsrv", TELNET_ADDCMD_FNAME);

  if (addcmd != NULL) {
    rfsimulator->poll_telnetcmdq =  (poll_telnetcmdq_func_t)get_shlibmodule_fptr("telnetsrv", TELNET_POLLCMDQ_FNAME);
    addcmd("rfsimu",rfsimu_vardef,rfsimu_cmdarray);

    for(int i=0; rfsimu_cmdarray[i].cmdfunc != NULL; i++) {
      if (  rfsimu_cmdarray[i].qptr != NULL) {
        rfsimulator->telnetcmd_qid = rfsimu_cmdarray[i].qptr;
        break;
      }
    }
  }

  return 0;
}
