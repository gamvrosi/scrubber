#!/bin/bash
timestamp=`date +%H%M%S_%d%b%y`
filename="results"$timestamp".m"
segsizes="64 128 256 512 1024 2048 4096 8192 16384 32768
          65536 131072 262144"
regsizes="8192 16384 32768 65536 131072 262144 524288 1048576
          2097152 4194304 8388608 16777216 33554432 35565552"

echo "% Sequential Scrubbing results:" > $filename
echo "SEQ_SS = [];" >> $filename
echo "SEQ_UT = [];" >> $filename
echo "SEQ_ST = [];" >> $filename
echo "SEQ_TT = [];" >> $filename
echo "SEQCPU = [];" >> $filename
echo "SEQMEM = [];" >> $filename

for ss in $segsizes
do
  echo "%  Using segment size: "$ss" bytes." >> $filename
  format="SEQ_SS = [SEQ_SS "$ss"];\n"\
"SEQ_UT = [SEQ_UT %U];\n"\
"SEQ_ST = [SEQ_ST %S];\n"\
"SEQ_TT = [SEQ_TT %e];\n"\
"SEQCPU = [SEQCPU ((%U+%S)/%e)];\n"\
"SEQMEM = [SEQMEM %K];"
  sudo time -a -o $filename -f "$format" ./reader -v -t SEQL -s $ss /dev/sdb1
done

echo "% Staggered Scrubbing (1MB segments, Vers. regions) results:" >> $filename
ss=1024
echo "STG1_SS = [];" >> $filename
echo "STG1_UT = [];" >> $filename
echo "STG1_ST = [];" >> $filename
echo "STG1_TT = [];" >> $filename
echo "STG1CPU = [];" >> $filename
echo "STG1MEM = [];" >> $filename

for rs in $regsizes
do
  echo "%  Using region size: "$rs" bytes." >> $filename
  format="STG1_SS = [STG1_SS "$rs"];\n"\
"STG1_UT = [STG1_UT %U];\n"\
"STG1_ST = [STG1_ST %S];\n"\
"STG1_TT = [STG1_TT %e];\n"\
"STG1CPU = [STG1CPU ((%U+%S)/%e)];\n"\
"STG1MEM = [STG1MEM %K];"
  sudo time -a -o $filename -f "$format" ./reader -v -t STAG -s $ss -r $rs /dev/sdb1
done

echo "% Staggered Scrubbing (2MB segments, Vers. regions) results:" >> $filename
ss=2048
echo "STG2_SS = [];" >> $filename
echo "STG2_UT = [];" >> $filename
echo "STG2_ST = [];" >> $filename
echo "STG2_TT = [];" >> $filename
echo "STG2CPU = [];" >> $filename
echo "STG2MEM = [];" >> $filename

for rs in $regsizes
do
  echo "%  Using region size: "$rs" bytes." >> $filename
  format="STG2_SS = [STG2_SS "$rs"];\n"\
"STG2_UT = [STG2_UT %U];\n"\
"STG2_ST = [STG2_ST %S];\n"\
"STG2_TT = [STG2_TT %e];\n"\
"STG2CPU = [STG2CPU ((%U+%S)/%e)];\n"\
"STG2MEM = [STG2MEM %K];"
  sudo time -a -o $filename -f "$format" ./reader -v -t STAG -s $ss -r $rs /dev/sdb1
done

echo "% Staggered Scrubbing (Vers. segments, 128MB regions) results:" >> $filename
rs=131072
echo "STG3_SS = [];" >> $filename
echo "STG3_UT = [];" >> $filename
echo "STG3_ST = [];" >> $filename
echo "STG3_TT = [];" >> $filename
echo "STG3CPU = [];" >> $filename
echo "STG3MEM = [];" >> $filename

for ss in $segsizes
do
  echo "%  Using segment size: "$ss" bytes." >> $filename
  format="STG3_SS = [STG3_SS "$ss"];\n"\
"STG3_UT = [STG3_UT %U];\n"\
"STG3_ST = [STG3_ST %S];\n"\
"STG3_TT = [STG3_TT %e];\n"\
"STG3CPU = [STG3CPU ((%U+%S)/%e)];\n"\
"STG3MEM = [STG3MEM %K];"
  sudo time -a -o $filename -f "$format" ./reader -v -t STAG -s $ss -r $rs /dev/sdb1
done

echo "% Staggered Scrubbing (Vers. segments, 256MB regions) results:" >> $filename
rs=262144
echo "STG4_SS = [];" >> $filename
echo "STG4_UT = [];" >> $filename
echo "STG4_ST = [];" >> $filename
echo "STG4_TT = [];" >> $filename
echo "STG4CPU = [];" >> $filename
echo "STG4MEM = [];" >> $filename

for ss in $segsizes
do
  echo "%  Using segment size: "$ss" bytes." >> $filename
  format="STG4_SS = [STG4_SS "$ss"];\n"\
"STG4_UT = [STG4_UT %U];\n"\
"STG4_ST = [STG4_ST %S];\n"\
"STG4_TT = [STG4_TT %e];\n"\
"STG4CPU = [STG4CPU ((%U+%S)/%e)];\n"\
"STG4MEM = [STG4MEM %K];"
  sudo time -a -o $filename -f "$format" ./reader -v -t STAG -s $ss -r $rs /dev/sdb1
done

