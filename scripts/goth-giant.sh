cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/userprog/args-dbl-space.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
make tests/userprog/bad-write2.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
