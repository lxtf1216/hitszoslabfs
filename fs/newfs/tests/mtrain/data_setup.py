import numpy as np
import torch
import random
from torchvision.datasets import MNIST
from torchvision import transforms

from utils import info

def scale_MNIST(dataset, num_samples_per_class, n):
    """
    Trim a dataset to have a specified number of samples per class,
    which is further divided equally into n datasets.
    """
    
    indices = []
    
    for class_label in range(10):
        class_indices = [
            i for i, label in enumerate(dataset.targets) if label == class_label
        ]
        sel_indices = torch.randperm(len(class_indices))[:num_samples_per_class]
        indices.extend([class_indices[i] for i in sel_indices])
    random.shuffle(indices)
    
    n_indices = np.split(np.array(indices), n)

    return [torch.utils.data.Subset(dataset, torch.tensor(subset)) for subset in n_indices]

def extract(subset):
    """
    extract images and labels from a subset and return as tensors
    """
    images = []
    labels = []
    for img, label in subset:
        images.append(img)
        labels.append(label)
    images = torch.stack(images)
    labels = torch.tensor(labels)
    return images, labels

def main():
    t = transforms.Compose([
        transforms.ToTensor(),
    ])
    train_set = MNIST(
        root='data',
        train=True,
        download=True,
        transform=t,
    )
    test_set = MNIST(
        root='data',
        train=False,
        download=True,
        transform=t,
    )

    num_samples_per_class_train = 320
    num_samples_per_class_test = 32
    train_subsets = scale_MNIST(train_set, num_samples_per_class_train, 10)
    test_subsets = scale_MNIST(test_set, num_samples_per_class_test, 10)

    scaled_train_sets = [extract(subset) for subset in train_subsets]
    scaled_test_sets = [extract(subset) for subset in test_subsets]

    # distribute to separate files
    import sys
    import os
    path_prefix = f"{sys.argv[1]}/trimmed" if len(sys.argv) > 1 else "./trimmed"
    if not os.path.exists(path_prefix):
        os.makedirs(path_prefix)
    for i, (train_data, test_data) in enumerate(zip(scaled_train_sets, scaled_test_sets)):
        torch.save(train_data, f'{path_prefix}/scaled_mnist_train_{i}.pt')
        torch.save(test_data, f'{path_prefix}/scaled_mnist_test_{i}.pt')


if __name__ == '__main__':
    main()
    info("data_setup.py: data setup complete")