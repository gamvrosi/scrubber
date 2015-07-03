% Starting Experiments for sdd (to find verify times)
% -----------------------------------------------

SS = [];
TIME = [];
SS = [SS 1];
%  Using segment size: 1 Kbytes
TIME = [TIME 8166/1000.0];
SS = [SS 2];
%  Using segment size: 2 Kbytes
TIME = [TIME 8306/1000.0];
SS = [SS 4];
%  Using segment size: 4 Kbytes
TIME = [TIME 8384/1000.0];
SS = [SS 8];
%  Using segment size: 8 Kbytes
TIME = [TIME 8403/1000.0];
SS = [SS 16];
%  Using segment size: 16 Kbytes
TIME = [TIME 8550/1000.0];
SS = [SS 32];
%  Using segment size: 32 Kbytes
TIME = [TIME 8456/1000.0];
SS = [SS 64];
%  Using segment size: 64 Kbytes
TIME = [TIME 8817/1000.0];
SS = [SS 128];
%  Using segment size: 128 Kbytes
TIME = [TIME 9548/1000.0];
SS = [SS 256];
%  Using segment size: 256 Kbytes
TIME = [TIME 10537/1000.0];
SS = [SS 512];
%  Using segment size: 512 Kbytes
TIME = [TIME 12395/1000.0];
SS = [SS 1024];
%  Using segment size: 1024 Kbytes
TIME = [TIME 16340/1000.0];
SS = [SS 2048];
%  Using segment size: 2048 Kbytes
TIME = [TIME 23590/1000.0];
SS = [SS 4096];
%  Using segment size: 4096 Kbytes
TIME = [TIME 38457/1000.0];
SS = [SS 8192];
%  Using segment size: 8192 Kbytes
TIME = [TIME 68388/1000.0];
SS = [SS 16384];
%  Using segment size: 16384 Kbytes
TIME = [TIME 127921/1000.0];
