cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/userprog/sc-boundary.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
make tests/userprog/child-bad.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
