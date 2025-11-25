import torch
import torch.nn as nn

from utils import info

class TinyVGG(nn.Module):
    """
    TinyVGG model architecture.
    """
    def __init__(
        self,
        in_channels=1,
        hidden_units=8,
        out_features=10,
    ):
        super().__init__()
        self.layer_stack = nn.Sequential(
            nn.Conv2d(in_channels=in_channels, out_channels=hidden_units, kernel_size=3, padding=1),
            nn.ReLU(),
            nn.Conv2d(in_channels=hidden_units, out_channels=hidden_units, kernel_size=3, padding=1),
            nn.ReLU(),
            nn.MaxPool2d(kernel_size=2, stride=2),
            nn.Conv2d(in_channels=hidden_units, out_channels=hidden_units, kernel_size=3, padding=1),
            nn.ReLU(),
            nn.Conv2d(in_channels=hidden_units, out_channels=hidden_units, kernel_size=3, padding=1),
            nn.ReLU(),
            nn.MaxPool2d(kernel_size=2, stride=2),

            nn.Flatten(),
            nn.Linear(in_features=hidden_units * 7 * 7, out_features=out_features),
        )

    def forward(self, x):
        return self.layer_stack(x)
    
def main():
    """
    Build a new model and save it.
    """
    model = TinyVGG()
    
    import sys, os
    path_prefix = f"{sys.argv[1]}/model" if len(sys.argv) > 1 else "./model"
    if not os.path.exists(path_prefix):
        os.makedirs(path_prefix)
    torch.save(model.state_dict(), f"{path_prefix}/model.pth")
    
if __name__ == "__main__":
    main()
    info("model.py: model built and saved successfully.")