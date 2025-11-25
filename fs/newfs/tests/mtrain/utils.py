import torch

def info(msg):
    BLUE = '\033[94m'
    RST = '\033[0m'
    print(f"{BLUE}{msg}{RST}")

def merge_subsets(n_subsets, path):
    train_prefix = f"{path}/scaled_mnist_train_"
    test_prefix = f"{path}/scaled_mnist_test_"
    train_sets = [torch.load(f"{train_prefix}{i}.pt") for i in range(n_subsets)]
    test_sets = [torch.load(f"{test_prefix}{i}.pt") for i in range(n_subsets)]
    train_set = (
        torch.cat([train_sets[i][0] for i in range(n_subsets)], dim=0), # images
        torch.cat([train_sets[i][1] for i in range(n_subsets)], dim=0)  # labels
    )
    test_set = (
        torch.cat([test_sets[i][0] for i in range(n_subsets)], dim=0),   
        torch.cat([test_sets[i][1] for i in range(n_subsets)], dim=0)    
    )
    return train_set, test_set


def load(
    data,   # ([samples], [labels])
    batch_size=32,
):
    data_loader = torch.utils.data.DataLoader(
        dataset=torch.utils.data.TensorDataset(*data),
        batch_size=batch_size,
        shuffle=True,
    )
    return data_loader