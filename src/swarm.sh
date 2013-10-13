#!/bin/bash

# To use this script you must define the following variables in the "swarm.conf"
# file. In addition, you must run the following commands if you do not have an
# ssh key that does not require a password: (use all of the default values for
# ssh-keygen by pressing enter)
#
# > ssh-keygen
# > cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys
#
# This will allow this computer to connect to any of the other CS computers
# without prompting you.

# DEFINE THE FOLLOWING VARIABLES IN THE "swarm.conf" FILE:"
# # The pintos module that you want to check
# module="filesys"
# # The location in which you will store the scripts the hosts will use to run the tests
# scripts_loc="$HOME/cs439/scripts"
# # The hosts that you want to use to run tests
# hosts="aero big-league-chew bit-o-honey black-black butterfinger candy-corn comma-chameleon cornish crunch cuddlefish disco-bandit dots envy fast-break fatsis gluttony gonyea goth-giant greed gummi-bears hasselblad heath hezrou hundred-grand inskeep jelly-belly kestenbaum ki-rin krackel lust mars mentos milky-way minolta minox mondello montagne mounds nikon octopus-gardener paper-golem pentax pez pop-rocks prakash pride razzles ring-pops sloth three-musketeers tin-of-spinach tinning-kit titanothere toblerone tootsie-rolls totenberg turtles twix twizzlers twoflower ulaby weretaco wrath ydstie"

# SCRIPT STARTS HERE

# Captures CTRL+C signals and kills all children
function killprocesses {
  jobs -p | xargs kill
  exit 0
}
trap killprocesses SIGINT

# A function to check if a folder exists
function checkfile {
  if [ ! -f $1 ]; then
    echo $2
    exit 0
  fi
}

# A function to check if a directory exists
function checkdir {
  if [ ! -d $1 ]; then
    echo $2
    exit 0
  fi
}

# Assume that the script is in the root pintos directory and get the pintos path
SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
pintos_loc=$DIR

# Make sure the config file exists
checkfile $pintos_loc/swarm.conf "Please create \"swarm.conf\" before running the script"
. ./swarm.conf

# Make sure the results directory exists
checkdir $scripts_loc "Please create or change \""$scripts_loc"\" before running the script"

# Make sure the pintos module exists
if [ ! -z "$1" ]; then
  module=$1
fi
if [ -z "$module" ]; then
  echo "Please specify a module to check"
fi
module_loc=$pintos_loc/$module
checkdir $module_loc "Pintos module \"$module\" does not exist"

# Find the number of cores and set the j flag accordingly
j=$(nproc)
((j=(j*2)+1))
j=-j$j

# Clean and build pintos
echo "Cleaning..."
cd $module_loc
make clean > /dev/null
echo "Building..."
make $j > /dev/null
cd build
module_loc=$module_loc/build
make $j > /dev/null
if [[ $? -ne 0 ]]; then
  echo -e "\033[31;1mBuild failed\033[0m"
  exit 1
fi

echo "Preparing scripts..."
# Prepare the scripts for execution
max=0
for host in $hosts
do
  echo "cd $module_loc" > "$scripts_loc/$host.sh"
  chmod u+x "$scripts_loc/$host.sh"
  (( max++ ))
done

# Add the tests to the script
i=0
num_tests=0
#tests=$(find $module_loc/tests)
tests="filesys threads userprog vm"
for t in $tests
do
  t=${t#$module_loc/tests/}
  t=${t#$module_loc/tests}
  files=$(find $module_loc/tests/$t -type f | grep -v \\.)
  for f in $files
  do
    f=${f#$module_loc/tests/$t}
    n=0
    for host in $hosts
    do
      if (( i==n )); then
        echo "make tests/$t$f.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null" >> "$scripts_loc/$host.sh"
      fi
      (( n++ ))
    done
    (( i++ ))
    (( num_tests++ ))
    if (( i==max )); then
      i=0
    fi
  done
done

# Execute the scripts
echo -e "Starting \033[32m$num_tests\033[0m tests from \033[32;1m$module_loc\033[0m on \033[36;1m$max\033[0m hosts (\033[36m$hosts\033[0m)"
for host in $hosts
do
  ssh $host -o ForwardX11=no "cd $scripts_loc;./$host.sh" &
done

wait

echo -e "\033[32;1mDone running tests. Results:\033[0m"
make $j check | egrep --color=always '^FAIL|$'

exit 0
