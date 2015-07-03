% Starting Experiments for sdd (to find verify times)
% -----------------------------------------------

SS = [];
TIME = [];
SS = [SS 1];
%  Using segment size: 1 Kbytes
TIME = [TIME 286/1000.0];
SS = [SS 2];
%  Using segment size: 2 Kbytes
TIME = [TIME 339/1000.0];
SS = [SS 4];
%  Using segment size: 4 Kbytes
TIME = [TIME 257/1000.0];
SS = [SS 8];
%  Using segment size: 8 Kbytes
TIME = [TIME 315/1000.0];
SS = [SS 16];
%  Using segment size: 16 Kbytes
TIME = [TIME 372/1000.0];
SS = [SS 32];
%  Using segment size: 32 Kbytes
TIME = [TIME 480/1000.0];
SS = [SS 64];
%  Using segment size: 64 Kbytes
TIME = [TIME 599/1000.0];
SS = [SS 128];
%  Using segment size: 128 Kbytes
TIME = [TIME 1081/1000.0];
SS = [SS 256];
%  Using segment size: 256 Kbytes
TIME = [TIME 1994/1000.0];
SS = [SS 512];
%  Using segment size: 512 Kbytes
TIME = [TIME 3999/1000.0];
SS = [SS 1024];
%  Using segment size: 1024 Kbytes
TIME = [TIME 7882/1000.0];
SS = [SS 2048];
%  Using segment size: 2048 Kbytes
TIME = [TIME 15121/1000.0];
SS = [SS 4096];
%  Using segment size: 4096 Kbytes
TIME = [TIME 29935/1000.0];
SS = [SS 8192];
%  Using segment size: 8192 Kbytes
TIME = [TIME 59982/1000.0];
SS = [SS 16384];
%  Using segment size: 16384 Kbytes
TIME = [TIME 119940/1000.0];
