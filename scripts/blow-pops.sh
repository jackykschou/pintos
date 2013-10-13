cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/filesys/base/lg-seq-random.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
make tests/userprog/exec-arg.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
