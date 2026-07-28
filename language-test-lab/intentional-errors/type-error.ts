interface User {
  id: string;
  active: boolean;
}

const user: User = {
  id: 42,
  active: "yes",
};

console.log(user.missingProperty);
