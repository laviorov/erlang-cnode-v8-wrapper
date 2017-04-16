const fs = require('fs');

fs.readFile('./small.json', (err, data) => {
  let arr = [];
  for(let i = 0; i < 90000; i++) {
    arr.push(JSON.parse(data));
  }
  fs.writeFile('./big.json', JSON.stringify(arr));
});
