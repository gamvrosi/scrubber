%
% exprand :: Function exporting exponentially distributed numbers to a unix
%            file
%
% Arg: - N = number of exponential random numbers
%      - filename = name of the file where the numbers will be
%                   extracted to
%
function [] = exprand(N, filename)

    nums = exprnd(1,N,1);
    dlmwrite(filename, nums, 'newline', 'unix');