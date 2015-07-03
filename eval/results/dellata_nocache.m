% Starting Experiments for sdc (to find verify times)
% -----------------------------------------------

SS = [];
TIME = [];
SS = [SS 1];
%  Using segment size: 1 Kbytes
TIME = [TIME 8166/1000.0];
SS = [SS 2];
%  Using segment size: 2 Kbytes
TIME = [TIME 8296/1000.0];
SS = [SS 4];
%  Using segment size: 4 Kbytes
TIME = [TIME 8331/1000.0];
SS = [SS 8];
%  Using segment size: 8 Kbytes
TIME = [TIME 8291/1000.0];
SS = [SS 16];
%  Using segment size: 16 Kbytes
TIME = [TIME 8348/1000.0];
SS = [SS 32];
%  Using segment size: 32 Kbytes
TIME = [TIME 8481/1000.0];
SS = [SS 64];
%  Using segment size: 64 Kbytes
TIME = [TIME 8754/1000.0];
SS = [SS 128];
%  Using segment size: 128 Kbytes
TIME = [TIME 9309/1000.0];
SS = [SS 256];
%  Using segment size: 256 Kbytes
TIME = [TIME 10399/1000.0];
SS = [SS 512];
%  Using segment size: 512 Kbytes
TIME = [TIME 12620/1000.0];
SS = [SS 1024];
%  Using segment size: 1024 Kbytes
TIME = [TIME 17087/1000.0];
SS = [SS 2048];
%  Using segment size: 2048 Kbytes
TIME = [TIME 25822/1000.0];
SS = [SS 4096];
%  Using segment size: 4096 Kbytes
TIME = [TIME 43597/1000.0];
SS = [SS 8192];
%  Using segment size: 8192 Kbytes
TIME = [TIME 79055/1000.0];
SS = [SS 16384];
%  Using segment size: 16384 Kbytes
TIME = [TIME 149840/1000.0];
