function Lpp = read_dat(filename)

w = 849;
h = 358;

f=fopen('out.dat','rb');
L=fread(f,w*h*3,'int32');
Lp=reshape(L,[w,h,3]);
Lpp=permute(Lp,[2 1 3]);
fclose(f);
