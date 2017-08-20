#include <stdio.h>

int main(int argc,char **argv)
{
  FILE *f=fopen("ascii.h","w");
  for(int i=0;i<256;i++) fprintf(f,"#pragma charmap (0x%02x,0x%02x)\n",i,i);
  fclose(f);
}
