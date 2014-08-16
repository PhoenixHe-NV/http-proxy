#!/bin/bash
# Author Yejiajie geniusye@geniusye.com
# 2nd Version Modifier Zhouyuwei 11302010067@fudan.edu.cn
# 3rd Version Modifier Jixiaofeng 11302010002@fudan.edu.cn
###################################################
# delete all the created file and kill the process
###################################################
function DoReap(){
	rm -rf proxy.log
	rm -rf tmp >& /dev/null

	killall proxy

	# kill all the ./proxy process
	#ID=`ps -ef | grep "./proxy" | grep -v "$0" | grep -v "grep" | awk '{print $2}'`
	#for id in $ID
	#do
	#	kill -9 $id >& /dev/null
	#	#echo "killed $id"
	#done
}

score=0;

totalScore=60
partAScore=30

##########################
# Compile the lab
##########################
make clean >& /dev/null
make > /dev/null
	#echo $?
if [[ $? != 0 ]]; then
	echo ""
	echo "[ * _ * ]Compile Error"
	echo ""
	popd >& /dev/null
	exit 1
fi;

DoReap
mkdir -p tmp
export http_proxy=localhost:8005
./proxy 8005 &

echo "Start Grading the lab..."
echo ''

##################################
# Part A: a single-thread proxy
#
# 1. check a simple web page
# 2. check a large page
# 3. check the log
##################################
echo '#####################################'
echo '# Part A  -  A single-thread proxy'
echo '#####################################'
echo ''
echo "Check for a simple web page"
wget http://www.tcs.fudan.edu.cn/~liy/courses/dm2/index.shtml -O tmp/test1-file -o wget.log
diff tmp/test1-file trace/test1-trace  >& tmp/test1-result
if [ -s tmp/test1-result ]; then
	echo 'Fail'
else
	let score+=10
	echo "OK"
fi;

echo "Check for a large file"
wget http://www.jwc.fudan.edu.cn/picture/article/67/b4/a4/193dd881400ea6ed9c84c1a107a3/40e48939-92fc-4bc6-a459-d15ac73aa647.pdf -O tmp/test2-file -o wget.log
diff tmp/test2-file trace/test2-trace >& tmp/test2-result
if [ -s tmp/test2-result ]; then
	echo "Fail"
else
	let score+=10
	echo 'OK'
fi;

echo "Check for the log file"
grep -e " [0-9]\{2\}:[0-9]\{2\}:[0-9]\{2\} CST: 127.0.0.1 http://www.tcs.fudan.edu.cn/~liy/courses/dm2/index.shtml" proxy.log -n | tail -n 1 > tmp/test3-result
if [ -s tmp/test3-result ]; then
	let score+=5
fi;
grep -e " [0-9]\{2\}:[0-9]\{2\}:[0-9]\{2\} CST: 127.0.0.1 http://www.jwc.fudan.edu.cn/picture/article/67/b4/a4/193dd881400ea6ed9c84c1a107a3/40e48939-92fc-4bc6-a459-d15ac73aa647.pdf" proxy.log -n | tail -n 1 >> tmp/test3-result
if [ -s tmp/test3-result ]; then
	let score+=5
	echo "OK"
else
	echo 'Fail'
fi;

echo "# Part A Score: $score/$partAScore"
echo ''
echo ''

####################################
# Part B: a multiple-thread proxy
#
# check for multiple thread 
####################################
echo '#####################################'
echo '# Part B - A multiple-thread proxy'
echo '#####################################'
echo ''

echo "Check for a normal web page"
wget https://www.kernel.org/pub/linux/kernel/people/geoff/cell/ps3-linux-docs/CellProgrammingTutorial/BasicsOfSIMDProgramming.html -O tmp/test4-file -o wget.log
diff tmp/test4-file trace/test4-trace  >& tmp/test4-result
if [ -s tmp/test4-result ]; then
	echo 'Fail'
else
	let score+=10
	echo "OK"
fi;


echo "Check the multiple request"
let multiple_thread=0
wget http://research.microsoft.com/pubs/192937/Transactions-APSIPA.pdf -O tmp/test5_0-file -o wget.log & >& /dev/null
sleep 1
#start a second request
wget http://www.jwc.fudan.edu.cn/picture/article/67/b7/6e/31c481df49bda3863d89cf80079b/782a3ab9-0bd6-4868-86f2-a035974cf260.pdf -O tmp/test5-file -o wget.log
ID=`ps -ef | grep "http://research.microsoft.com/pubs/192937/Transactions-APSIPA.pdf" | grep -v "$0" | grep -v "grep" | awk '{print $2}'`
for id in $ID
do
	let multiple_thread=1
	#echo "kill the $id"
	kill -9 $id >& /dev/null
done
diff tmp/test5-file trace/test5-trace >& tmp/test5-result
if [ -s tmp/test5-result ] || [ $multiple_thread -eq 0 ]; then
	echo 'Fail'
else
	let score+=20
	echo "OK"
fi;


echo "# Total Score: $score / $totalScore"
echo ""

#DoReap
