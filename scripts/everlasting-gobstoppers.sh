cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/filesys/base/child-syn-wrt.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
make tests/userprog/rox-simple.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
