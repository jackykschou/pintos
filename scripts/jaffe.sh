cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/userprog/sc-boundary-2.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
make tests/userprog/bad-jump.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
