cd /v/filer4b/v38q001/jackyc/CS439/pintos/src/userprog/build
make tests/userprog/no-vm/multi-oom.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
make tests/userprog/rox-child.result | tee >(egrep '^pass') >(egrep --color=always '^FAIL') > /dev/null
