% Starting Experiments for sde (to find verify times)
% -----------------------------------------------

SS = [];
TIME = [];
SS = [SS 1];
%  Using segment size: 1 Kbytes
TIME = [TIME 3827/1000.0];
SS = [SS 2];
%  Using segment size: 2 Kbytes
TIME = [TIME 3840/1000.0];
SS = [SS 4];
%  Using segment size: 4 Kbytes
TIME = [TIME 3858/1000.0];
SS = [SS 8];
%  Using segment size: 8 Kbytes
TIME = [TIME 4072/1000.0];
SS = [SS 16];
%  Using segment size: 16 Kbytes
TIME = [TIME 4156/1000.0];
SS = [SS 32];
%  Using segment size: 32 Kbytes
TIME = [TIME 4158/1000.0];
SS = [SS 64];
%  Using segment size: 64 Kbytes
TIME = [TIME 4636/1000.0];
SS = [SS 128];
%  Using segment size: 128 Kbytes
TIME = [TIME 5187/1000.0];
SS = [SS 256];
%  Using segment size: 256 Kbytes
TIME = [TIME 6546/1000.0];
SS = [SS 512];
%  Using segment size: 512 Kbytes
TIME = [TIME 9363/1000.0];
SS = [SS 1024];
%  Using segment size: 1024 Kbytes
TIME = [TIME 14869/1000.0];
SS = [SS 2048];
%  Using segment size: 2048 Kbytes
TIME = [TIME 25819/1000.0];
SS = [SS 4096];
%  Using segment size: 4096 Kbytes
TIME = [TIME 47652/1000.0];
SS = [SS 8192];
%  Using segment size: 8192 Kbytes
TIME = [TIME 91643/1000.0];
SS = [SS 16384];
%  Using segment size: 16384 Kbytes
TIME = [TIME 181334/1000.0];
