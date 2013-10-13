cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/userprog/args-many.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
make tests/userprog/bad-read2.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
