cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/userprog/create-empty.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
