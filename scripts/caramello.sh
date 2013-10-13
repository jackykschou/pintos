cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/filesys/base/sm-create.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
make tests/userprog/exec-multiple.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
