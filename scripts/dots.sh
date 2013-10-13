cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/filesys/base/syn-read.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
make tests/userprog/wait-killed.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
