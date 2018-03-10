//------------------------------------------    
//    shmem hist 
//------------------------------------------    

#define shm_hist_NB 512
#define shm_hist_LT 256
typedef struct  {  
  float xmin,xmax,xbin;
  unsigned int id,nbin,over,under,integral,entries;
  char        title[shm_hist_LT];
  unsigned int hist[shm_hist_NB];
} SHM_HIST;

//------------------------------------------    

char* mprint(unsigned int  *hist, int nbin, const char *title);
float mstati(int, unsigned int  *hist, int nbin);
float gauss(void);
void mreset(unsigned int *hist, int n);
void mcopy(unsigned int *hist1, unsigned int *hist2, int n);
void mfill(unsigned int *hist, int n, float x, float w);
void mf1(unsigned int *hist, int n, int x);
//---------------------------------------------------

char* hprint(SHM_HIST  *hp, int id);
void hbook1(SHM_HIST *hp, int id, char *title, int nbin, float  xmin,  float xmax);
void hreset0(SHM_HIST *hp, int nh);
void hreset(SHM_HIST *hp, int id);
float hstati(int sw, SHM_HIST  *hp, int id);
void hf1(SHM_HIST *hp, int id, unsigned int ich);
int hnoent(SHM_HIST  *hp,  int id);
//---------------------------------------------------
